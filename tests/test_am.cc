
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
#include "util/bytevec.hh"
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

::rlc_errno init_am(fixture::rlc_ctx &fx, ::rlc_backend *backend,
                    event::event_handler &events)
{
        auto status = ::rlc_init(fx.get(), backend, mem::alloc, mem::alloc);
        if (status != 0) {
                return status;
        }

        return fx.attach_listener(events.listener());
}

/* Spec 6.2.3.6: D/C is the MSB of the first octet, 1 for data. Read raw
 * because proto::am::header::decode() asserts the bit is set. */
bool pdu_is_data(::gabs_pbuf buf)
{
        auto it = ::gabs_pbuf_ci_init(&buf);
        auto byte0 = reinterpret_cast<const std::uint8_t *>(
                ::gabs_pbuf_ci_data(it))[0];

        return (byte0 & 0x80) != 0;
}

/* Only meaningful when pdu_is_data(buf). Every context here is
 * RLC_SN_18BIT. */
proto::am::header pdu_header(::gabs_pbuf buf)
{
        auto bytes = buf::pbuf_ptr::from_weak(buf).vec();
        auto it = bytes.cbegin();

        return proto::am::header::decode(it, proto::snwidth::W18);
}

std::uint32_t pdu_sn(::gabs_pbuf buf)
{
        return pdu_header(buf).sn;
}

/* Loopback wiring: a PDU submitted on one side goes straight into the
 * other's rlc_rx_submit, unless `drop` says it was lost. */
struct peer_link {
        ::rlc_context *self = nullptr;
        ::rlc_context *other = nullptr;
        std::function<bool(::gabs_pbuf)> drop = [](::gabs_pbuf) {
                return false;
        };
        /* Reordering: a PDU the predicate holds is delivered after the one
         * behind it, swapping the two on the wire. */
        std::function<bool(::gabs_pbuf)> hold = [](::gabs_pbuf) {
                return false;
        };
        std::optional<::gabs_pbuf> held;
        /* SN and segment offset of each data PDU, in the order the peer
         * received them. */
        std::vector<std::pair<std::uint32_t, std::uint32_t>> arrived;

        ~peer_link()
        {
                if (held.has_value()) {
                        ::gabs_pbuf_decref(held.value());
                }
        }
};

/* Drops the first `count` data (or control) PDUs. These need not be the
 * same PDU retransmitted, if several are in flight. */
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

/* Drops every data (or control) PDU, for a direction that never gets
 * through. */
std::function<bool(::gabs_pbuf)> drop_always(bool want_data)
{
        return [want_data](::gabs_pbuf buf) {
                return pdu_is_data(buf) == want_data;
        };
}

/* Holds the first data (or control) PDU back. */
std::function<bool(::gabs_pbuf)> hold_first(bool want_data)
{
        return [want_data, done = false](::gabs_pbuf buf) mutable {
                if (done || pdu_is_data(buf) != want_data) {
                        return false;
                }

                done = true;
                return true;
        };
}

/* Holds back the first AMD PDU with the given SN. */
std::function<bool(::gabs_pbuf)> hold_sn(std::uint32_t sn)
{
        return [sn, done = false](::gabs_pbuf buf) mutable {
                if (done || !pdu_is_data(buf) || pdu_sn(buf) != sn) {
                        return false;
                }

                done = true;
                return true;
        };
}

/* Drops the first AMD PDU with the given SN, losing one whole SDU out of
 * several queued. */
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

void deliver(peer_link &link, ::gabs_pbuf buf)
{
        if (pdu_is_data(buf)) {
                auto hdr = pdu_header(buf);

                link.arrived.emplace_back(hdr.sn, hdr.so.value_or(0));
        }

        ::rlc_rx_submit(link.other, buf);
}

backend::backend make_peer_backend(peer_link &link)
{
        return backend::backend(
                [&link](::rlc_context *, ::gabs_pbuf buf) -> int {
                        if (link.drop(buf)) {
                                ::gabs_pbuf_decref(buf);
                                return 0;
                        }

                        if (!link.held.has_value() && link.hold(buf)) {
                                link.held = buf;
                                return 0;
                        }

                        deliver(link, buf);

                        if (link.held.has_value()) {
                                auto held = link.held.value();

                                link.held.reset();
                                deliver(link, held);
                        }

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

        REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_DONE).payload ==
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

        /* RX_Next is 0 and AM_Window_Size is 131072 for an 18 bit SN. */
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

        /* A gap between the first and last segment leaves SN=0 detectably
         * incomplete, which starts t-Reassembly. */
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
        (void)events.pop(::rlc_event::RLC_EVENT_RX_DONE);

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

        REQUIRE(events.pop(::rlc_event::RLC_EVENT_TX_RELEASE).sn == 0);
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

        REQUIRE(events_b.pop(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(content));
        REQUIRE(events_b.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM peers recover a lost data segment via a poll-triggered STATUS",
         "[am][loopback]")
{
        /* Spec 5.3.3.2, 5.3.4 and 5.3.2 together: a PDU that empties the
         * buffer is polled, the poll triggers a STATUS report, and its
         * NACK retransmits the missing range. One pump() drains the whole
         * cascade. Runs the loss on each link in turn. */
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

        REQUIRE(receiver_events.pop(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(content));
        REQUIRE(receiver_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM TX recovers from a lost STATUS via t-PollRetransmit",
         "[am][loopback]")
{
        /* Spec 5.3.3.4: with the ack lost, t-PollRetransmit expiry
         * retransmits with a fresh poll, so the receiver reports status
         * again. Runs the loss on each link in turn. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::manual_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        /* STATUS flows back on the other link from the data. */
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

        /* The data arrived but its ack was lost. The dropped attempt still
         * started t-StatusProhibit, which must expire before a retry. */
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

        (void)sender_events.pop(::rlc_event::RLC_EVENT_TX_RELEASE);
        REQUIRE(sender_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM peers recover multiple lost segments of the same SDU",
         "[am][loopback]")
{
        /* Losing several PDUs of one SDU, but staying under
         * maxRetxThreshold, still recovers within a single pump(). Runs
         * the loss on each link in turn. */
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

        REQUIRE(receiver_events.pop(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(content));
        REQUIRE(receiver_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM TX gives up and fails the SDU after too many losses",
         "[am][loopback]")
{
        /* Spec 5.3.2: reaching maxRetxThreshold gives up on the SDU and
         * tells upper layers. The direction never gets through, so every
         * retry is lost too; each fire() sends one retry. Failure and
         * success both report RLC_EVENT_TX_RELEASE, so they are told apart
         * by the receiver having gotten nothing. */
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
        REQUIRE(sender_events.pop(::rlc_event::RLC_EVENT_TX_RELEASE).sn == 0);
        REQUIRE(sender_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM peers advance the window and deliver in order around a "
         "dropped middle SDU",
         "[am][loopback]")
{
        /* Three separately queued SDUs, the middle one lost. Spec
         * 5.2.3.2.1/5.2.3.2.3: RX_Next only advances past a completed SDU
         * at the window base, so delivery stalls at the gap and the last
         * SDU is withheld until the middle one is recovered. Runs the loss
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
                        receiver_events.pop(::rlc_event::RLC_EVENT_RX_DONE);

                REQUIRE(ev.payload == to_bytevec(content));
        }
        REQUIRE(receiver_events.empty());

        REQUIRE(::rlc_window_base(&receiver.get()->rx.win) == 3);

        /* Only SDU 0 is acked so far: the second STATUS report is held
         * back by t-StatusProhibit from the first. */
        REQUIRE(sender_events.pop(::rlc_event::RLC_EVENT_TX_RELEASE).sn == 0);
        REQUIRE(sender_events.empty());

        REQUIRE(gabs_override::armed(
                       receiver_link.self->arq.t_status_prohibit.gtimer) ==
               true);
        gabs_override::fire(
                receiver_link.self->arq.t_status_prohibit.gtimer);

        pump(link_a, link_b, 64);

        /* SDU 0 was released above, so the remaining two arrive now. */
        REQUIRE(sender_events.size() == 2);
        (void)sender_events.pop(::rlc_event::RLC_EVENT_TX_RELEASE);
        (void)sender_events.pop(::rlc_event::RLC_EVENT_TX_RELEASE);
        REQUIRE(sender_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM peers reassemble an SDU whose segments arrive out of order",
         "[am][loopback]")
{
        /* Spec 5.2.3.2.2/5.2.3.2.3: byte segments may arrive in any order,
         * and the SDU is delivered once every byte is there. Swapping two
         * segments on the wire reaches that through the link rather than by
         * feeding rlc_rx_submit by hand, so RX_Next_Highest and t-Reassembly
         * see the gap the way they would on a reordering link. Runs the
         * reordering on each link in turn. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        unsigned int sent = 0;
        auto &data_link = a_sends ? link_a : link_b;

        data_link.hold = hold_first(true);
        data_link.drop = [&sent](::gabs_pbuf buf) {
                sent += pdu_is_data(buf);
                return false;
        };

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_am(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_am(peer_b, backend_b, events_b) == 0);

        auto &sender = a_sends ? peer_a : peer_b;
        auto &receiver = a_sends ? peer_b : peer_a;
        auto &receiver_events = a_sends ? events_b : events_a;

        /* 6.2.2.4: a 3 octet header on the first segment and 5 on the rest,
         * so a 20 byte grant carries 17 bytes and then 15. */
        const unsigned int segments = 4;

        std::string content(50, 'r');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(sender.get(), sdu, nullptr) == 0);

        pump(link_a, link_b, 20);

        REQUIRE(receiver_events.pop(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(content));
        REQUIRE(receiver_events.empty());

        /* The second segment arrived before the first. */
        REQUIRE(data_link.arrived[0].second != 0);
        REQUIRE(data_link.arrived[1].second == 0);

        /* Reordering alone costs no retransmission: every PDU the sender
         * put on the wire was a segment of the SDU. */
        REQUIRE(sent == segments);

        REQUIRE(::rlc_window_base(&receiver.get()->rx.win) == 1);

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

TEST_CASE("AM peers deliver in order when an SDU overtakes its predecessor",
         "[am][loopback]")
{
        /* Spec 5.2.3.2.3: RX_Next only advances past a completed SDU at the
         * window base, so an SDU that arrives early is held back until the
         * one before it lands, and both go up in SN order. Runs the
         * reordering on each link in turn. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        event::event_handler events_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        auto &data_link = a_sends ? link_a : link_b;

        data_link.hold = hold_sn(0);

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_am(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_am(peer_b, backend_b, events_b) == 0);

        auto &sender = a_sends ? peer_a : peer_b;
        auto &receiver = a_sends ? peer_b : peer_a;
        auto &receiver_events = a_sends ? events_b : events_a;

        std::string first = "sdu zero, held back";
        std::string second = "sdu one, overtakes it";

        REQUIRE(::rlc_tx(sender.get(), buf::create(first), nullptr) == 0);
        REQUIRE(::rlc_tx(sender.get(), buf::create(second), nullptr) == 0);

        pump(link_a, link_b, 64);

        REQUIRE(receiver_events.pop(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(first));
        REQUIRE(receiver_events.pop(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(second));
        REQUIRE(receiver_events.empty());

        /* SN 1 reached the peer first, but SN 0 was delivered first. */
        REQUIRE(data_link.arrived[0].first == 1);
        REQUIRE(data_link.arrived[1].first == 0);

        REQUIRE(::rlc_window_base(&receiver.get()->rx.win) == 2);

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

}; // namespace rlc::test
