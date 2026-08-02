
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>

#include "util/mem.hh"
#include "util/buf.hh"
#include "util/backend.hh"
#include "util/fixture.hh"

#include "gabs-overrides/timer/timer.hh"

namespace rlc::test
{

using namespace util;

namespace
{

template <class Container>
std::vector<std::byte> to_bytevec(const Container &c)
{
        std::vector<std::byte> ret(c.size());

        std::transform(c.begin(), c.end(), ret.begin(),
                      [](auto v) { return static_cast<std::byte>(v); });

        return ret;
}

struct captured_event {
        int type;
        std::uint32_t sn;
        std::vector<std::byte> payload;
};

/* Builds a fixture::rlc_ctx listener_fn that records events into a
 * caller-owned vector, so each TEST_CASE keeps its own event storage in
 * local scope instead of it living inside a fixture struct. */
fixture::rlc_ctx::listener_fn capture_into(std::vector<captured_event> &events)
{
        return [&events](const ::rlc_event &ev) {
                captured_event e{static_cast<int>(ev.type), 0, {}};

                if (ev.type == ::rlc_event::RLC_EVENT_RX_DONE_DIRECT) {
                        /* rlc_event's payload is a union: for this type the
                         * live member is a gabs_pbuf*, so there is no SDU to
                         * read an SN from. pbuf_ptr takes ownership and
                         * decrefs on scope exit, hence the incref. */
                        ::gabs_pbuf_incref(*ev.buf);
                        e.payload = buf::pbuf_ptr(*ev.buf).vec();
                } else if (ev.sdu != nullptr) {
                        e.sn = ev.sdu->sn;
                }

                events.push_back(std::move(e));
        };
}

/* rlc_init always installs the AM default_config; every context in this
 * file is switched to RLC_TM right after init. The SN width and the timer
 * durations are never consulted in transparent mode. */
const ::rlc_config tm_conf = {
        .type = RLC_TM,
        .window_size = 10,
        .pdu_without_poll_max = 3,
        .byte_without_poll_max = 1500,
        .time_reassembly_us = 500000,
        .time_poll_retransmit_us = 50000,
        .time_status_prohibit_us = 5000,
        .max_retx_threshhold = 3,
        .sn_width = RLC_SN_12BIT,
};

::rlc_errno init_tm(fixture::rlc_ctx &fx, ::rlc_backend *backend,
                    std::vector<captured_event> &events)
{
        auto status = ::rlc_init(fx.get(), backend, mem::alloc, mem::alloc);
        if (status != 0) {
                return status;
        }

        ::rlc_set_config(fx.get(), &tm_conf);

        return fx.attach_listener(capture_into(events));
}

/* Peer-to-peer wiring, as in test_am.cc/test_um.cc. A TMD PDU carries no
 * header at all, so there is nothing to classify a dropped packet by -
 * loss here is purely positional. */
struct peer_link {
        ::rlc_context *self = nullptr;
        ::rlc_context *other = nullptr;
        std::function<bool(::gabs_pbuf)> drop = [](::gabs_pbuf) {
                return false;
        };
};

/* Drops the first `count` PDUs submitted on a link and forwards everything
 * else. */
std::function<bool(::gabs_pbuf)> drop_up_to(int count)
{
        return [count](::gabs_pbuf) mutable {
                if (count > 0) {
                        count--;
                        return true;
                }

                return false;
        };
}

std::function<bool(::gabs_pbuf)> drop_first()
{
        return drop_up_to(1);
}

backend::backend make_peer_backend(peer_link &link)
{
        return backend::backend(
                [&link](::rlc_context *, ::gabs_pbuf buf) -> int {
                        if (link.drop(buf)) {
                                ::gabs_pbuf_decref(buf);
                                return 0;
                        }

                        ::rlc_rx_submit(link.other, buf);
                        return 0;
                },
                [](::rlc_context *) -> int { return 0; });
}

void pump(peer_link &a, peer_link &b, std::size_t mtu)
{
        for (int i = 0; i < 64; i++) {
                auto remain_a = ::rlc_tx_avail(a.self, mtu);
                auto remain_b = ::rlc_tx_avail(b.self, mtu);

                if (remain_a == mtu && remain_b == mtu) {
                        return;
                }
        }

        FAIL("pump() did not settle");
}

} // namespace

TEST_CASE("TM RX delivers a received PDU unmodified", "[tm][rx]")
{
        /* Spec 5.2.1.2.1 and 4.2.1.1.3: a TMD PDU is just an RLC SDU, and
         * is delivered to the upper layer without any modification. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        std::vector<captured_event> events;
        REQUIRE(init_tm(fx, back, events) == 0);

        std::string payload = "a transparent mode SDU, carried verbatim";

        ::rlc_rx_submit(fx.get(), buf::create(payload).strong());

        REQUIRE(events.size() == 1);
        REQUIRE(events[0].type ==
               static_cast<int>(::rlc_event::RLC_EVENT_RX_DONE_DIRECT));
        REQUIRE(events[0].payload == to_bytevec(payload));

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("TM TX submits an SDU without adding a header", "[tm][tx]")
{
        /* Spec 5.2.1.1.1 and 4.2.1.1.2: the SDU is submitted to the lower
         * layer without modification, and no RLC header is included. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        std::vector<captured_event> events;
        REQUIRE(init_tm(fx, back, events) == 0);

        std::string content = "no header goes in front of this";
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(fx.get(), sdu, nullptr) == 0);

        (void)::rlc_tx_avail(fx.get(), gabs_pbuf_size(sdu));

        REQUIRE(tx_queue.size() == 1);
        REQUIRE(tx_queue.front().vec() == to_bytevec(content));

        REQUIRE(events.size() == 1);
        REQUIRE(events[0].type ==
               static_cast<int>(::rlc_event::RLC_EVENT_TX_RELEASE));

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("TM TX does not segment an SDU that exceeds the opportunity",
         "[tm][tx]")
{
        /* Spec 4.2.1.1.2: a transmitting TM RLC entity shall not segment
         * the RLC SDUs, so an opportunity too small to carry the whole SDU
         * yields nothing at all, and the SDU waits for a larger one. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        std::vector<captured_event> events;
        REQUIRE(init_tm(fx, back, events) == 0);

        std::string content(40, 'x');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(fx.get(), sdu, nullptr) == 0);

        (void)::rlc_tx_avail(fx.get(), content.size() - 1);
        REQUIRE(tx_queue.empty());

        (void)::rlc_tx_avail(fx.get(), content.size());

        REQUIRE(tx_queue.size() == 1);
        REQUIRE(tx_queue.front().vec() == to_bytevec(content));

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("TM peers exchange an SDU end-to-end", "[tm][loopback]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        std::vector<captured_event> events_a;
        fixture::rlc_ctx peer_b;
        std::vector<captured_event> events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_tm(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_tm(peer_b, backend_b, events_b) == 0);

        std::string content(30, 'z');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(peer_a.get(), sdu, nullptr) == 0);

        pump(link_a, link_b, 64);

        REQUIRE(events_b.size() == 1);
        REQUIRE(events_b[0].type ==
               static_cast<int>(::rlc_event::RLC_EVENT_RX_DONE_DIRECT));
        REQUIRE(events_b[0].payload == to_bytevec(content));

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("TM peers deliver several SDUs in order", "[tm][loopback]")
{
        /* Each SDU maps to exactly one TMD PDU, so what the receiver sees
         * is the sequence the sender queued. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        std::vector<captured_event> events_a;
        fixture::rlc_ctx peer_b;
        std::vector<captured_event> events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_tm(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_tm(peer_b, backend_b, events_b) == 0);

        std::string first = "first SDU";
        std::string second = "second SDU";
        std::string third = "third SDU";

        REQUIRE(::rlc_tx(peer_a.get(), buf::create(first), nullptr) == 0);
        REQUIRE(::rlc_tx(peer_a.get(), buf::create(second), nullptr) == 0);
        REQUIRE(::rlc_tx(peer_a.get(), buf::create(third), nullptr) == 0);

        pump(link_a, link_b, 64);

        REQUIRE(events_b.size() == 3);
        REQUIRE(events_b[0].payload == to_bytevec(first));
        REQUIRE(events_b[1].payload == to_bytevec(second));
        REQUIRE(events_b[2].payload == to_bytevec(third));

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("TM peers permanently lose a dropped PDU", "[tm][loopback]")
{
        /* Spec 5.3.1: ARQ procedures are only performed by an AM RLC
         * entity, and transparent mode keeps no reassembly state either, so
         * a lost TMD PDU is simply gone - the sender still counts it as
         * released and nothing is retransmitted. Runs with the loss on each
         * link in turn. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        std::vector<captured_event> events_a;
        fixture::rlc_ctx peer_b;
        std::vector<captured_event> events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        (a_sends ? link_a : link_b).drop = drop_first();

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_tm(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_tm(peer_b, backend_b, events_b) == 0);

        auto &sender = a_sends ? peer_a : peer_b;
        auto &sender_events = a_sends ? events_a : events_b;
        auto &receiver_events = a_sends ? events_b : events_a;

        std::string content(30, 'y');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(sender.get(), sdu, nullptr) == 0);

        pump(link_a, link_b, 64);

        REQUIRE(receiver_events.empty());
        REQUIRE(sender_events.size() == 1);
        REQUIRE(sender_events[0].type ==
               static_cast<int>(::rlc_event::RLC_EVENT_TX_RELEASE));

        pump(link_a, link_b, 64);

        REQUIRE(receiver_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

}; // namespace rlc::test
