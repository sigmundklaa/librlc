
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>

#include "util/mem.hh"
#include "util/buf.hh"
#include "util/backend.hh"
#include "util/proto.hh"

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

/*
 * ctx->listener is a plain C function pointer with no user-data slot, so
 * `ctx` is embedded in this fixture and gabs_container_of recovers it (and
 * its local `events` vector) from the ctx pointer the listener is called
 * with - same pattern as rx_fixture/arq_fixture in the other test files.
 */
struct am_fixture {
        ::rlc_context ctx{};
        std::vector<captured_event> events;
};

void capture_listener(::rlc_context *raw_ctx, const ::rlc_event *ev)
{
        auto *fx = gabs_container_of(raw_ctx, am_fixture, ctx);
        captured_event e{static_cast<int>(ev->type), 0, {}};

        if (ev->sdu != nullptr) {
                e.sn = ev->sdu->sn;

                if (ev->type == ::rlc_event::RLC_EVENT_RX_DONE) {
                        /* pbuf_ptr takes ownership and decrefs on scope
                         * exit, so incref the SDU's still-owned buffer to
                         * keep it balanced. */
                        ::gabs_pbuf_incref(ev->sdu->rx.buffer.buf);
                        e.payload =
                                buf::pbuf_ptr(ev->sdu->rx.buffer.buf).vec();
                }
        }

        fx->events.push_back(std::move(e));
}

::rlc_errno init_am(am_fixture &fx, ::rlc_backend *backend)
{
        auto status = ::rlc_init(&fx.ctx, backend, mem::alloc, mem::alloc);
        if (status != 0) {
                return status;
        }

        return ::rlc_attach_listener(&fx.ctx, capture_listener);
}

/* Peer-to-peer wiring for the loopback tests: submitting a PDU on one side
 * delivers it directly into the other side's rlc_rx_submit. rlc_tx_avail
 * only sends what fits in one opportunity - a multi-segment SDU needs
 * several calls - so pump() grants opportunities to both sides repeatedly
 * (like a MAC polling on its own schedule) until neither has anything left
 * to send, rather than relying solely on tx_request. */
struct peer_link {
        ::rlc_context *self = nullptr;
        ::rlc_context *other = nullptr;
};

backend::backend make_peer_backend(peer_link &link)
{
        return backend::backend(
                [&link](::rlc_context *, ::gabs_pbuf buf) -> int {
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

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

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
        ::rlc_rx_submit(&fx.ctx, buf::create(last_bytes).strong());
        REQUIRE(fx.events.empty());

        ::rlc_rx_submit(&fx.ctx, buf::create(first_bytes).strong());

        REQUIRE(fx.events.size() == 1);
        REQUIRE(fx.events[0].type ==
               static_cast<int>(::rlc_event::RLC_EVENT_RX_DONE));
        REQUIRE(fx.events[0].payload == to_bytevec(payload));
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

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

        auto w = proto::snwidth::W18;

        /* RX_Next starts at 0, AM_Window_Size = 131072 for 18-bit SN; a SN
         * far beyond that is outside the window. */
        proto::am::header hdr{false, proto::seginfo::ALL, 200000,
                              std::nullopt};
        auto bytes = hdr.encode(w);
        auto payload = to_bytevec(std::string("unreachable"));
        bytes.insert(bytes.end(), payload.begin(), payload.end());

        ::rlc_rx_submit(&fx.ctx, buf::create(bytes).strong());

        REQUIRE(fx.events.empty());
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

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

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

        ::rlc_rx_submit(&fx.ctx, buf::create(first_bytes).strong());
        ::rlc_rx_submit(&fx.ctx, buf::create(last_bytes).strong());
        REQUIRE(fx.events.empty());

        REQUIRE(gabs_override::armed(fx.ctx.rx.t_reassembly.gtimer) == true);
        gabs_override::fire(fx.ctx.rx.t_reassembly.gtimer);

        REQUIRE(fx.ctx.arq.gen_status == true);
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

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

        auto w = proto::snwidth::W18;

        proto::am::header hdr{false, proto::seginfo::ALL, 0, std::nullopt};
        auto bytes = hdr.encode(w);
        auto payload = to_bytevec(std::string("no duplicates please"));
        bytes.insert(bytes.end(), payload.begin(), payload.end());

        auto pdu = buf::create(bytes);

        ::rlc_rx_submit(&fx.ctx, pdu.strong());
        REQUIRE(fx.events.size() == 1);

        ::rlc_rx_submit(&fx.ctx, pdu.strong());

        /* Only the first delivery counts. */
        REQUIRE(fx.events.size() == 1);
}

TEST_CASE("AM RX discards a PDU with a reserved CPT value", "[am][rx]")
{
        /* Spec 5.6.1: a PDU with reserved or invalid values is discarded. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

        auto w = proto::snwidth::W18;

        proto::am::status status{0, {}};
        auto bytes = status.encode(w);
        /* CPT occupies bits 4-6 of octet 1; 0b001 is reserved. */
        bytes[0] |= static_cast<std::byte>(0b001 << 4);

        ::rlc_rx_submit(&fx.ctx, buf::create(bytes).strong());

        REQUIRE(fx.events.empty());
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

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

        auto conf = *::rlc_get_config(&fx.ctx);
        conf.pdu_without_poll_max = 1;
        ::rlc_set_config(&fx.ctx, &conf);

        auto sdu = buf::create(std::string("poll me"));
        REQUIRE(::rlc_tx(&fx.ctx, sdu, nullptr) == 0);

        (void)::rlc_tx_avail(&fx.ctx, gabs_pbuf_size(sdu) + 8);
        REQUIRE(!tx_queue.empty());

        auto [header, data] =
                proto::am::split_data(tx_queue.front(), proto::snwidth::W18);
        REQUIRE(header.polled == true);
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

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

        auto conf = *::rlc_get_config(&fx.ctx);
        conf.pdu_without_poll_max = 1;
        ::rlc_set_config(&fx.ctx, &conf);

        auto sdu = buf::create(std::string("poll me"));
        REQUIRE(::rlc_tx(&fx.ctx, sdu, nullptr) == 0);
        (void)::rlc_tx_avail(&fx.ctx, gabs_pbuf_size(sdu) + 8);
        REQUIRE(tx_queue.size() == 1);
        tx_queue.pop();

        REQUIRE(gabs_override::armed(
                       fx.ctx.arq.t_poll_retransmit.gtimer) == true);
        gabs_override::fire(fx.ctx.arq.t_poll_retransmit.gtimer);

        (void)::rlc_tx_avail(&fx.ctx, gabs_pbuf_size(sdu) + 8);
        REQUIRE(!tx_queue.empty());

        auto [header, data] =
                proto::am::split_data(tx_queue.front(), proto::snwidth::W18);
        REQUIRE(header.polled == true);
        REQUIRE(data == sdu.vec());
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

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

        std::string content = "0123456789";
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(&fx.ctx, sdu, nullptr) == 0);
        (void)::rlc_tx_avail(&fx.ctx, gabs_pbuf_size(sdu) + 8);
        REQUIRE(tx_queue.size() == 1);
        tx_queue.pop();

        auto w = proto::snwidth::W18;
        proto::am::status_part part{0, std::uint16_t(2), std::uint16_t(5),
                                    std::nullopt};
        proto::am::status status{0, {part}};

        ::rlc_rx_submit(&fx.ctx, buf::create(status.encode(w)).strong());

        (void)::rlc_tx_avail(&fx.ctx, gabs_pbuf_size(sdu) + 8);
        REQUIRE(!tx_queue.empty());

        auto [header, data] = proto::am::split_data(tx_queue.front(), w);
        REQUIRE(header.si == proto::seginfo::NEITHER);
        REQUIRE(header.so.value() == 2);
        REQUIRE(data == to_bytevec(content.substr(2, 3)));
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

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

        auto sdu = buf::create(std::string("ack me"));
        REQUIRE(::rlc_tx(&fx.ctx, sdu, nullptr) == 0);
        (void)::rlc_tx_avail(&fx.ctx, gabs_pbuf_size(sdu) + 8);

        auto w = proto::snwidth::W18;
        proto::am::status status{1, {}};

        ::rlc_rx_submit(&fx.ctx, buf::create(status.encode(w)).strong());

        REQUIRE(fx.events.size() == 1);
        REQUIRE(fx.events[0].type ==
               static_cast<int>(::rlc_event::RLC_EVENT_TX_RELEASE));
        REQUIRE(fx.events[0].sn == 0);
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

        am_fixture fx;
        REQUIRE(init_am(fx, back) == 0);

        auto w = proto::snwidth::W18;

        auto polled_pdu = [&](std::uint32_t sn) {
                proto::am::header hdr{true, proto::seginfo::ALL, sn,
                                      std::nullopt};
                auto bytes = hdr.encode(w);
                auto payload = to_bytevec(std::string("x"));
                bytes.insert(bytes.end(), payload.begin(), payload.end());
                return bytes;
        };

        ::rlc_rx_submit(&fx.ctx, buf::create(polled_pdu(0)).strong());
        REQUIRE(fx.ctx.arq.gen_status == true);

        (void)::rlc_tx_avail(&fx.ctx, 64);
        REQUIRE(tx_queue.size() == 1);
        tx_queue.pop();
        REQUIRE(gabs_override::armed(
                       fx.ctx.arq.t_status_prohibit.gtimer) == true);

        ::rlc_rx_submit(&fx.ctx, buf::create(polled_pdu(1)).strong());
        REQUIRE(fx.ctx.arq.gen_status == true);

        /* Still prohibited: no second STATUS goes out yet. */
        (void)::rlc_tx_avail(&fx.ctx, 64);
        REQUIRE(tx_queue.empty());

        gabs_override::fire(fx.ctx.arq.t_status_prohibit.gtimer);

        (void)::rlc_tx_avail(&fx.ctx, 64);
        REQUIRE(!tx_queue.empty());
}

TEST_CASE("AM peers exchange a segmented SDU end-to-end", "[am][loopback]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        am_fixture peer_a;
        am_fixture peer_b;

        peer_link link_a{&peer_a.ctx, &peer_b.ctx};
        peer_link link_b{&peer_b.ctx, &peer_a.ctx};

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_am(peer_a, backend_a) == 0);
        REQUIRE(init_am(peer_b, backend_b) == 0);

        std::string content(50, 'z');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(&peer_a.ctx, sdu, nullptr) == 0);

        pump(link_a, link_b, 20);

        REQUIRE(peer_b.events.size() == 1);
        REQUIRE(peer_b.events[0].type ==
               static_cast<int>(::rlc_event::RLC_EVENT_RX_DONE));
        REQUIRE(peer_b.events[0].payload == to_bytevec(content));
}

TEST_CASE("AM peers recover a lost segment via a poll-triggered STATUS",
         "[am][loopback]")
{
        /* Spec 5.3.3.2: an AMD PDU that empties the transmission buffer is
         * always polled. Spec 5.3.4: the receiving side triggers a STATUS
         * report for a polled PDU. Spec 5.3.2: the resulting NACK causes
         * peer_a to retransmit exactly the missing byte range. A single
         * pump() drains the whole cascade - send, drop, poll, STATUS,
         * retransmit, deliver - with no manual timer intervention needed. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        am_fixture peer_a;
        am_fixture peer_b;

        peer_link link_a{&peer_a.ctx, &peer_b.ctx};
        peer_link link_b{&peer_b.ctx, &peer_a.ctx};

        /* peer_a's backend drops the first submitted PDU (simulating a
         * lost segment) and forwards every subsequent one normally. */
        int drop_remaining = 1;
        auto backend_a = backend::backend(
                [&](::rlc_context *, ::gabs_pbuf buf) -> int {
                        if (drop_remaining > 0) {
                                drop_remaining--;
                                ::gabs_pbuf_decref(buf);
                                return 0;
                        }

                        ::rlc_rx_submit(link_a.other, buf);
                        return 0;
                },
                [](::rlc_context *) -> int { return 0; });
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_am(peer_a, backend_a) == 0);
        REQUIRE(init_am(peer_b, backend_b) == 0);

        std::string content(50, 'y');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(&peer_a.ctx, sdu, nullptr) == 0);

        pump(link_a, link_b, 20);

        REQUIRE(peer_b.events.size() == 1);
        REQUIRE(peer_b.events[0].type ==
               static_cast<int>(::rlc_event::RLC_EVENT_RX_DONE));
        REQUIRE(peer_b.events[0].payload == to_bytevec(content));
}

}; // namespace rlc::test
