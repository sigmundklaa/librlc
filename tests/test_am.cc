
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>

#include "util/mem.hh"
#include "util/buf.hh"
#include "util/backend.hh"
#include "util/event.hh"
#include "util/proto.hh"
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

::rlc_errno init_am(fixture::rlc_ctx &fx, ::rlc_backend *backend,
                    event::event_handler &events)
{
        auto status = ::rlc_init(fx.get(), backend, mem::alloc, mem::alloc);
        if (status != 0) {
                return status;
        }

        return fx.attach_listener(events.listener());
}

/* Spec 6.2.3.6: D/C is the MSB of a PDU's first octet - 1 for an AMD (data)
 * PDU, 0 for a STATUS (control) PDU. proto::am::header::decode() asserts
 * this bit is set (it only models AMD headers), so it can't be used for
 * the classification itself - just this one raw peek, which reads
 * without touching buf's refcount so the caller keeps full ownership
 * either way. */
bool pdu_is_data(::gabs_pbuf buf)
{
        auto it = ::gabs_pbuf_ci_init(&buf);
        auto byte0 = reinterpret_cast<const std::uint8_t *>(
                ::gabs_pbuf_ci_data(it))[0];

        return (byte0 & 0x80) != 0;
}

/* Only meaningful when pdu_is_data(buf) is true. Every context in this
 * file uses the default RLC_SN_18BIT config. Increfs buf for the
 * duration, since pbuf_ptr takes ownership and decrefs on scope exit. */
std::uint32_t pdu_sn(::gabs_pbuf buf)
{
        ::gabs_pbuf_incref(buf);
        auto bytes = buf::pbuf_ptr(buf).vec();
        auto it = bytes.cbegin();

        return proto::am::header::decode(it, proto::snwidth::W18).sn;
}

/* Peer-to-peer wiring for the loopback tests: submitting a PDU on one side
 * delivers it directly into the other side's rlc_rx_submit, unless `drop`
 * says to simulate the packet being lost in transit. rlc_tx_avail only
 * sends what fits in one opportunity - a multi-segment SDU needs several
 * calls - so pump() grants opportunities to both sides repeatedly (like a
 * MAC polling on its own schedule) until neither has anything left to
 * send, rather than relying solely on tx_request. */
struct peer_link {
        ::rlc_context *self = nullptr;
        ::rlc_context *other = nullptr;
        std::function<bool(::gabs_pbuf)> drop = [](::gabs_pbuf) {
                return false;
        };
};

/* Drops the first `count` data (or, if !want_data, control) PDUs
 * submitted on a link and forwards everything else - simulating `count`
 * lost packets (not necessarily the same one retransmitted, if several
 * PDUs of the requested kind are already in flight before any retry). */
std::function<bool(::gabs_pbuf)> drop_up_to(bool want_data, int count)
{
        return [want_data, count](::gabs_pbuf buf) mutable {
                if (count > 0 && pdu_is_data(buf) == want_data) {
                        count--;
                        return true;
                }

                return false;
        };
}

std::function<bool(::gabs_pbuf)> drop_first(bool want_data)
{
        return drop_up_to(want_data, 1);
}

/* Drops every data (or control) PDU submitted on a link, unconditionally -
 * simulating a direction that never gets through at all. */
std::function<bool(::gabs_pbuf)> drop_always(bool want_data)
{
        return [want_data](::gabs_pbuf buf) {
                return pdu_is_data(buf) == want_data;
        };
}

/* Drops the first AMD PDU submitted on a link with the given SN, and
 * forwards everything else - simulating one specific SDU, out of several
 * independently queued ones, being lost entirely (as opposed to just one
 * segment of a single larger SDU). */
std::function<bool(::gabs_pbuf)> drop_sn(std::uint32_t sn)
{
        return [sn, dropped = false](::gabs_pbuf buf) mutable {
                if (dropped || !pdu_is_data(buf) || pdu_sn(buf) != sn) {
                        return false;
                }

                dropped = true;
                return true;
        };
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

TEST_CASE("AM RX reassembles a segmented SDU received out of order",
         "[am][rx]")
{
        /* Spec 5.2.3.2.2/5.2.3.2.3: byte segments of an RLC SDU may arrive
         * in any order; the SDU is delivered once every byte has been
         * received. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        auto w = proto::snwidth::W18;
        std::string payload = "hello world, this is a segmented AM SDU!";

        proto::am::header first{false, proto::seginfo::FIRST, 0,
                                std::nullopt};
        proto::am::header last{false, proto::seginfo::LAST, 0,
                               std::optional<std::uint16_t>(20)};

        auto first_bytes = first.encode(w);
        auto last_bytes = last.encode(w);

        auto seg1 = to_bytevec(payload.substr(0, 20));
        auto seg2 = to_bytevec(payload.substr(20));

        first_bytes.insert(first_bytes.end(), seg1.begin(), seg1.end());
        last_bytes.insert(last_bytes.end(), seg2.begin(), seg2.end());

        /* Deliver the last segment first. */
        ::rlc_rx_submit(fx.get(), buf::create(last_bytes).strong());
        REQUIRE(events.empty());

        ::rlc_rx_submit(fx.get(), buf::create(first_bytes).strong());

        REQUIRE(events.get(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(payload));
        REQUIRE(events.empty());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM RX discards an AMD PDU with SN outside the receiving window",
         "[am][rx]")
{
        /* Spec 5.2.3.2.2: a SN outside the receiving window is discarded. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        auto w = proto::snwidth::W18;

        /* RX_Next starts at 0, AM_Window_Size = 131072 for 18-bit SN; a SN
         * far beyond that is outside the window. */
        proto::am::header hdr{false, proto::seginfo::ALL, 200000,
                              std::nullopt};
        auto bytes = hdr.encode(w);
        auto payload = to_bytevec(std::string("unreachable"));
        bytes.insert(bytes.end(), payload.begin(), payload.end());

        ::rlc_rx_submit(fx.get(), buf::create(bytes).strong());

        REQUIRE(events.empty());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM RX triggers a STATUS report when t-Reassembly expires",
         "[am][rx]")
{
        /* Spec 5.3.4, "Detection of reception failure of an AMD PDU": the
         * receiving side shall trigger a STATUS report when t-Reassembly
         * expires. */
        gabs_override::timer_ctx timer_ctx(gabs_override::manual_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        auto w = proto::snwidth::W18;

        /* Deliver the first and last segments of SN=0 with a gap in
         * between, so the SDU is detectably incomplete and t-Reassembly
         * starts. */
        proto::am::header first{false, proto::seginfo::FIRST, 0,
                                std::nullopt};
        auto first_bytes = first.encode(w);
        auto seg1 = to_bytevec(std::string("first"));
        first_bytes.insert(first_bytes.end(), seg1.begin(), seg1.end());

        proto::am::header last{false, proto::seginfo::LAST, 0,
                               std::optional<std::uint16_t>(10)};
        auto last_bytes = last.encode(w);
        auto seg2 = to_bytevec(std::string("last!"));
        last_bytes.insert(last_bytes.end(), seg2.begin(), seg2.end());

        ::rlc_rx_submit(fx.get(), buf::create(first_bytes).strong());
        ::rlc_rx_submit(fx.get(), buf::create(last_bytes).strong());
        REQUIRE(events.empty());

        REQUIRE(gabs_override::armed(fx.get()->rx.t_reassembly.gtimer) == true);
        gabs_override::fire(fx.get()->rx.t_reassembly.gtimer);

        REQUIRE(fx.get()->arq.gen_status == true);

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM RX discards a duplicate AMD PDU segment", "[am][rx]")
{
        /* Spec 5.2.3.2.2: duplicate byte segments of an RLC SDU are
         * discarded. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        auto w = proto::snwidth::W18;

        proto::am::header hdr{false, proto::seginfo::ALL, 0, std::nullopt};
        auto bytes = hdr.encode(w);
        auto payload = to_bytevec(std::string("no duplicates please"));
        bytes.insert(bytes.end(), payload.begin(), payload.end());

        auto pdu = buf::create(bytes);

        ::rlc_rx_submit(fx.get(), pdu.strong());
        (void)events.get(::rlc_event::RLC_EVENT_RX_DONE);

        ::rlc_rx_submit(fx.get(), pdu.strong());

        /* Only the first delivery counts. */
        REQUIRE(events.empty());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM RX discards a PDU with a reserved CPT value", "[am][rx]")
{
        /* Spec 5.6.1: a PDU with reserved or invalid values is discarded. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        auto w = proto::snwidth::W18;

        proto::am::status status{0, {}};
        auto bytes = status.encode(w);
        /* CPT occupies bits 4-6 of octet 1; 0b001 is reserved. */
        bytes[0] |= static_cast<std::byte>(0b001 << 4);

        ::rlc_rx_submit(fx.get(), buf::create(bytes).strong());

        REQUIRE(events.empty());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM TX includes a poll once pollPDU is reached", "[am][tx]")
{
        /* Spec 5.3.3.2: a poll is included once PDU_WITHOUT_POLL >=
         * pollPDU. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        auto conf = *::rlc_get_config(fx.get());
        conf.pdu_without_poll_max = 1;
        ::rlc_set_config(fx.get(), &conf);

        auto sdu = buf::create(std::string("poll me"));
        REQUIRE(::rlc_tx(fx.get(), sdu, nullptr) == 0);

        (void)::rlc_tx_avail(fx.get(), gabs_pbuf_size(sdu) + 8);
        REQUIRE(!tx_queue.empty());

        auto [header, data] =
                proto::am::split_data(tx_queue.front(), proto::snwidth::W18);
        REQUIRE(header.polled == true);

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM TX retransmits the polled PDU when t-PollRetransmit expires",
         "[am][tx]")
{
        /* Spec 5.3.3.4: on expiry of t-PollRetransmit with nothing new to
         * send, retransmit and poll. */
        gabs_override::timer_ctx timer_ctx(gabs_override::manual_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        auto conf = *::rlc_get_config(fx.get());
        conf.pdu_without_poll_max = 1;
        ::rlc_set_config(fx.get(), &conf);

        auto sdu = buf::create(std::string("poll me"));
        REQUIRE(::rlc_tx(fx.get(), sdu, nullptr) == 0);
        (void)::rlc_tx_avail(fx.get(), gabs_pbuf_size(sdu) + 8);
        REQUIRE(tx_queue.size() == 1);
        tx_queue.pop();

        REQUIRE(gabs_override::armed(
                       fx.get()->arq.t_poll_retransmit.gtimer) == true);
        gabs_override::fire(fx.get()->arq.t_poll_retransmit.gtimer);

        (void)::rlc_tx_avail(fx.get(), gabs_pbuf_size(sdu) + 8);
        REQUIRE(!tx_queue.empty());

        auto [header, data] =
                proto::am::split_data(tx_queue.front(), proto::snwidth::W18);
        REQUIRE(header.polled == true);
        REQUIRE(data == sdu.vec());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM TX retransmits only the NACKed byte range from a STATUS PDU",
         "[am][tx]")
{
        /* Spec 5.3.2: NACK_SN with SOstart/SOend retransmits only that byte
         * range. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        std::string content = "0123456789";
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(fx.get(), sdu, nullptr) == 0);
        (void)::rlc_tx_avail(fx.get(), gabs_pbuf_size(sdu) + 8);
        REQUIRE(tx_queue.size() == 1);
        tx_queue.pop();

        auto w = proto::snwidth::W18;
        proto::am::status_part part{0, std::uint16_t(2), std::uint16_t(5),
                                    std::nullopt};
        proto::am::status status{0, {part}};

        ::rlc_rx_submit(fx.get(), buf::create(status.encode(w)).strong());

        (void)::rlc_tx_avail(fx.get(), gabs_pbuf_size(sdu) + 8);
        REQUIRE(!tx_queue.empty());

        auto [header, data] = proto::am::split_data(tx_queue.front(), w);
        REQUIRE(header.si == proto::seginfo::NEITHER);
        REQUIRE(header.so.value() == 2);
        REQUIRE(data == to_bytevec(content.substr(2, 3)));

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM TX advances TX_Next_Ack and releases the SDU on a positive "
         "ack",
         "[am][tx]")
{
        /* Spec 5.2.3.1: a positive acknowledgement advances TX_Next_Ack and
         * notifies upper layers of successful delivery. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        auto sdu = buf::create(std::string("ack me"));
        REQUIRE(::rlc_tx(fx.get(), sdu, nullptr) == 0);
        (void)::rlc_tx_avail(fx.get(), gabs_pbuf_size(sdu) + 8);

        auto w = proto::snwidth::W18;
        proto::am::status status{1, {}};

        ::rlc_rx_submit(fx.get(), buf::create(status.encode(w)).strong());

        REQUIRE(events.get(::rlc_event::RLC_EVENT_TX_RELEASE).sn == 0);
        REQUIRE(events.empty());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM RX collapses multiple STATUS triggers under t-StatusProhibit",
         "[am][rx]")
{
        /* Spec 5.3.4: while t-StatusProhibit is running, further triggers
         * are collapsed into a single STATUS PDU sent once it expires. */
        gabs_override::timer_ctx timer_ctx(gabs_override::manual_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_am(fx, back, events) == 0);

        auto w = proto::snwidth::W18;

        auto polled_pdu = [&](std::uint32_t sn) {
                proto::am::header hdr{true, proto::seginfo::ALL, sn,
                                      std::nullopt};
                auto bytes = hdr.encode(w);
                auto payload = to_bytevec(std::string("x"));
                bytes.insert(bytes.end(), payload.begin(), payload.end());
                return bytes;
        };

        ::rlc_rx_submit(fx.get(), buf::create(polled_pdu(0)).strong());
        REQUIRE(fx.get()->arq.gen_status == true);

        (void)::rlc_tx_avail(fx.get(), 64);
        REQUIRE(tx_queue.size() == 1);
        tx_queue.pop();
        REQUIRE(gabs_override::armed(
                       fx.get()->arq.t_status_prohibit.gtimer) == true);

        ::rlc_rx_submit(fx.get(), buf::create(polled_pdu(1)).strong());
        REQUIRE(fx.get()->arq.gen_status == true);

        /* Still prohibited: no second STATUS goes out yet. */
        (void)::rlc_tx_avail(fx.get(), 64);
        REQUIRE(tx_queue.empty());

        gabs_override::fire(fx.get()->arq.t_status_prohibit.gtimer);

        (void)::rlc_tx_avail(fx.get(), 64);
        REQUIRE(!tx_queue.empty());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("AM peers exchange a segmented SDU end-to-end", "[am][loopback]")
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

        REQUIRE(init_am(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_am(peer_b, backend_b, events_b) == 0);

        std::string content(50, 'z');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(peer_a.get(), sdu, nullptr) == 0);

        pump(link_a, link_b, 20);

        REQUIRE(events_b.get(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(content));
        REQUIRE(events_b.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM peers recover a lost data segment via a poll-triggered STATUS",
         "[am][loopback]")
{
        /* Spec 5.3.3.2: an AMD PDU that empties the transmission buffer is
         * always polled. Spec 5.3.4: the receiving side triggers a STATUS
         * report for a polled PDU. Spec 5.3.2: the resulting NACK causes
         * the sender to retransmit exactly the missing byte range. A
         * single pump() drains the whole cascade - send, drop, poll,
         * STATUS, retransmit, deliver - with no manual timer intervention
         * needed. Runs with the loss on each link in turn, so both the
         * A->B and B->A data directions are covered. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        (a_sends ? link_a : link_b).drop = drop_first(true);

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_am(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_am(peer_b, backend_b, events_b) == 0);

        auto &sender = a_sends ? peer_a : peer_b;
        auto &receiver = a_sends ? peer_b : peer_a;
        auto &sender_events = a_sends ? events_a : events_b;
        auto &receiver_events = a_sends ? events_b : events_a;

        std::string content(50, 'y');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(sender.get(), sdu, nullptr) == 0);

        pump(link_a, link_b, 20);

        REQUIRE(receiver_events.get(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(content));
        REQUIRE(receiver_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM TX recovers from a lost STATUS via t-PollRetransmit",
         "[am][loopback]")
{
        /* Spec 5.3.3.4: if the acknowledgement never arrives, expiry of
         * t-PollRetransmit makes the sender retransmit with a fresh poll,
         * giving the receiver another chance to report status - this time
         * without the control PDU being lost. Runs with the loss on each
         * link in turn, so both the A->B and B->A control directions are
         * covered. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::manual_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        /* The STATUS report flows back from the receiver to the sender,
         * i.e. on the *other* link from the data. */
        (a_sends ? link_b : link_a).drop = drop_first(false);

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_am(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_am(peer_b, backend_b, events_b) == 0);

        auto &sender = a_sends ? peer_a : peer_b;
        auto &receiver = a_sends ? peer_b : peer_a;
        auto &sender_events = a_sends ? events_a : events_b;
        auto &receiver_events = a_sends ? events_b : events_a;
        auto &sender_link = a_sends ? link_a : link_b;
        auto &receiver_link = a_sends ? link_b : link_a;

        std::string content(10, 'z');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(sender.get(), sdu, nullptr) == 0);

        pump(link_a, link_b, 30);

        /* The data got through fine, but its ack was lost in transit. The
         * receiver's own (dropped) attempt still started its
         * t-StatusProhibit, which needs to expire too before a retry can
         * go out. */
        REQUIRE(receiver_events.size() == 1);
        REQUIRE(sender_events.empty());

        REQUIRE(gabs_override::armed(
                       sender_link.self->arq.t_poll_retransmit.gtimer) ==
               true);
        gabs_override::fire(sender_link.self->arq.t_poll_retransmit.gtimer);

        REQUIRE(gabs_override::armed(
                       receiver_link.self->arq.t_status_prohibit.gtimer) ==
               true);
        gabs_override::fire(
                receiver_link.self->arq.t_status_prohibit.gtimer);

        pump(link_a, link_b, 30);

        (void)sender_events.get(::rlc_event::RLC_EVENT_TX_RELEASE);
        REQUIRE(sender_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM peers recover multiple lost segments of the same SDU",
         "[am][loopback]")
{
        /* Generalizes the single-lost-segment case: losing more than one
         * of a multi-segment SDU's PDUs, but staying under
         * maxRetxThreshold, still recovers via repeated poll-triggered
         * STATUS/NACK rounds within a single pump(). Runs with the loss
         * on each link in turn. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        (a_sends ? link_a : link_b).drop = drop_up_to(true, 2);

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_am(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_am(peer_b, backend_b, events_b) == 0);

        auto &sender = a_sends ? peer_a : peer_b;
        auto &receiver = a_sends ? peer_b : peer_a;
        auto &sender_events = a_sends ? events_a : events_b;
        auto &receiver_events = a_sends ? events_b : events_a;

        std::string content(50, 'y');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(sender.get(), sdu, nullptr) == 0);

        pump(link_a, link_b, 20);

        REQUIRE(receiver_events.get(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(content));
        REQUIRE(receiver_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM TX gives up and fails the SDU after too many losses",
         "[am][loopback]")
{
        /* Spec 5.3.2: once RETX_COUNT reaches maxRetxThreshold, the
         * sender gives up on the RLC SDU and notifies upper layers of the
         * failure. Modeled here as a direction that never gets through at
         * all, so every t-PollRetransmit-driven retry attempt is lost too
         * - each fire() only stages and sends one retry, so it's called
         * up to maxRetxThreshold times. Runs with the failing direction
         * on each link in turn.
         *
         * rlc_event_tx_fail and rlc_event_tx_done both report
         * RLC_EVENT_TX_RELEASE, so failure is distinguished from success
         * here by the receiver never having gotten anything. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::manual_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        (a_sends ? link_a : link_b).drop = drop_always(true);

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_am(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_am(peer_b, backend_b, events_b) == 0);

        auto &sender = a_sends ? peer_a : peer_b;
        auto &receiver = a_sends ? peer_b : peer_a;
        auto &sender_events = a_sends ? events_a : events_b;
        auto &receiver_events = a_sends ? events_b : events_a;

        auto conf = *::rlc_get_config(sender.get());
        conf.max_retx_threshhold = 2;
        ::rlc_set_config(sender.get(), &conf);

        std::string content(10, 'w');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(sender.get(), sdu, nullptr) == 0);

        pump(link_a, link_b, 30);

        /* maxRetxThreshold retransmissions are served before the limit is
         * reached, so the give-up happens on the round after them. */
        for (std::uint32_t i = 0; i < conf.max_retx_threshhold + 1; i++) {
                REQUIRE(gabs_override::armed(
                               sender.get()->arq.t_poll_retransmit.gtimer) ==
                       true);
                gabs_override::fire(
                        sender.get()->arq.t_poll_retransmit.gtimer);

                pump(link_a, link_b, 30);
        }

        REQUIRE(receiver_events.empty());
        REQUIRE(sender_events.get(::rlc_event::RLC_EVENT_TX_RELEASE).sn == 0);
        REQUIRE(sender_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM peers advance the window and deliver in order around a "
         "dropped middle SDU",
         "[am][loopback]")
{
        /* Three independently queued SDUs (not segments of one larger
         * SDU) with the middle one's sole PDU lost entirely. Spec
         * 5.2.3.2.1/5.2.3.2.3: RX_Next only advances past a completed SDU
         * at the window base, so it stalls at the missing one even
         * though the last SDU has already fully arrived; deliver_ready
         * only hands SDUs to the upper layer in contiguous SN order, so
         * the last SDU is withheld until the middle one is recovered -
         * then both are delivered together, in order. Runs with the loss
         * on each link in turn. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::manual_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        (a_sends ? link_a : link_b).drop = drop_sn(1);

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_am(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_am(peer_b, backend_b, events_b) == 0);

        auto &sender = a_sends ? peer_a : peer_b;
        auto &receiver = a_sends ? peer_b : peer_a;
        auto &sender_events = a_sends ? events_a : events_b;
        auto &receiver_events = a_sends ? events_b : events_a;
        auto &receiver_link = a_sends ? link_b : link_a;

        std::string content0 = "sdu zero";
        std::string content1 = "sdu one, dropped once";
        std::string content2 = "sdu two";

        REQUIRE(::rlc_tx(sender.get(), buf::create(content0), nullptr) == 0);
        REQUIRE(::rlc_tx(sender.get(), buf::create(content1), nullptr) == 0);
        REQUIRE(::rlc_tx(sender.get(), buf::create(content2), nullptr) == 0);

        pump(link_a, link_b, 64);

        /* SDU 0 is delivered right away; SDU 2 is complete on arrival but
         * withheld until SDU 1 is recovered, then both go out together. */
        for (const auto &content : {content0, content1, content2}) {
                const auto &ev =
                        receiver_events.get(::rlc_event::RLC_EVENT_RX_DONE);

                REQUIRE(ev.payload == to_bytevec(content));
        }
        REQUIRE(receiver_events.empty());

        REQUIRE(::rlc_window_base(&receiver.get()->rx.win) == 3);

        /* Only SDU 0 has been acked so far: the recovery of SDU 1 (and
         * the piggybacked ack for SDU 2) triggered another STATUS report,
         * but the receiver's own first STATUS already started its
         * t-StatusProhibit, so that second report is still pending. */
        REQUIRE(sender_events.get(::rlc_event::RLC_EVENT_TX_RELEASE).sn == 0);
        REQUIRE(sender_events.empty());

        REQUIRE(gabs_override::armed(
                       receiver_link.self->arq.t_status_prohibit.gtimer) ==
               true);
        gabs_override::fire(
                receiver_link.self->arq.t_status_prohibit.gtimer);

        pump(link_a, link_b, 64);

        /* SDU 0 was released above, so the remaining two arrive now. */
        REQUIRE(sender_events.size() == 2);
        (void)sender_events.get(::rlc_event::RLC_EVENT_TX_RELEASE);
        (void)sender_events.get(::rlc_event::RLC_EVENT_TX_RELEASE);
        REQUIRE(sender_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

}; // namespace rlc::test
