
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
#include "util/bytevec.hh"
#include "util/backend.hh"
#include "util/event.hh"
#include "util/fixture.hh"

#include "gabs-overrides/timer/timer.hh"

namespace rlc::test
{

using namespace util;

namespace
{

/* rlc_init installs the AM default_config, so every context here is
 * switched to RLC_TM after init. */
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
                    event::event_handler &events)
{
        auto status = ::rlc_init(fx.get(), backend, mem::alloc, mem::alloc);
        if (status != 0) {
                return status;
        }

        ::rlc_set_config(fx.get(), &tm_conf);

        return fx.attach_listener(events.listener());
}

/* Loopback wiring, as in test_am.cc. A TMD PDU has no header to classify
 * a dropped packet by, so loss is positional. */
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
        event::event_handler events;
        REQUIRE(init_tm(fx, back, events) == 0);

        std::string payload = "a transparent mode SDU, carried verbatim";

        ::rlc_rx_submit(fx.get(), buf::create(payload).strong());

        REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_DONE_DIRECT).payload ==
               to_bytevec(payload));
        REQUIRE(events.empty());

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
        event::event_handler events;
        REQUIRE(init_tm(fx, back, events) == 0);

        std::string content = "no header goes in front of this";
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(fx.get(), sdu, nullptr) == 0);

        (void)::rlc_tx_avail(fx.get(), gabs_pbuf_size(sdu));

        REQUIRE(tx_queue.size() == 1);
        REQUIRE(tx_queue.front().vec() == to_bytevec(content));

        (void)events.pop(::rlc_event::RLC_EVENT_TX_RELEASE);
        REQUIRE(events.empty());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("TM TX does not segment an SDU that exceeds the opportunity",
         "[tm][tx]")
{
        /* Spec 4.2.1.1.2: TM must not segment, so an opportunity too
         * small for the whole SDU yields nothing. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
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
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

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

        REQUIRE(events_b.pop(::rlc_event::RLC_EVENT_RX_DONE_DIRECT)
                       .payload == to_bytevec(content));
        REQUIRE(events_b.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("TM peers deliver several SDUs in order", "[tm][loopback]")
{
        /* Each SDU maps to exactly one TMD PDU, so what the receiver sees
         * is the sequence the sender queued. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

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

        for (const auto &content : {first, second, third}) {
                const auto &ev = events_b.pop(
                        ::rlc_event::RLC_EVENT_RX_DONE_DIRECT);

                REQUIRE(ev.payload == to_bytevec(content));
        }
        REQUIRE(events_b.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("TM peers permanently lose a dropped PDU", "[tm][loopback]")
{
        /* Spec 5.3.1: ARQ is AM only, and TM keeps no reassembly state,
         * so a lost PDU is simply gone and the sender still counts it
         * released. Runs the loss on each link in turn. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

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
        (void)sender_events.pop(::rlc_event::RLC_EVENT_TX_RELEASE);
        REQUIRE(sender_events.empty());

        pump(link_a, link_b, 64);

        REQUIRE(receiver_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

}; // namespace rlc::test
