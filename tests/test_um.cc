
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

/* rlc_init always installs the AM default_config; every context in this
 * file is switched to RLC_UM with a 12-bit SN right after init. */
const ::rlc_config um_conf = {
        .type = RLC_UM,
        .window_size = 10,
        .pdu_without_poll_max = 3,
        .byte_without_poll_max = 1500,
        .time_reassembly_us = 500000,
        .time_poll_retransmit_us = 50000,
        .time_status_prohibit_us = 5000,
        .max_retx_threshhold = 3,
        .sn_width = RLC_SN_12BIT,
};

::rlc_errno init_um(fixture::rlc_ctx &fx, ::rlc_backend *backend,
                    event::event_handler &events)
{
        auto status = ::rlc_init(fx.get(), backend, mem::alloc, mem::alloc);
        if (status != 0) {
                return status;
        }

        ::rlc_set_config(fx.get(), &um_conf);

        return fx.attach_listener(events.listener());
}

/* Peer-to-peer wiring, as in test_am.cc's peer_link/pump/make_peer_backend.
 * UM has no control PDUs (no D/C bit, no STATUS) so unlike the AM helpers,
 * loss here is purely positional - there is nothing to classify. */
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

TEST_CASE("UM RX delivers a reassembled SDU", "[um][rx]")
{
        /* Spec 5.2.2.2.2/5.2.2.2.3: byte segments of an RLC SDU may arrive
         * in any order; the SDU is delivered once every byte has been
         * received. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_um(fx, back, events) == 0);

        auto w = proto::snwidth::W12;
        std::string payload = "hello world, this is a segmented UM SDU!";

        proto::um::header first{proto::seginfo::FIRST, 0, std::nullopt};
        proto::um::header last{proto::seginfo::LAST, 0,
                               std::optional<std::uint16_t>(20)};

        auto first_bytes = first.encode(w);
        auto last_bytes = last.encode(w);

        auto seg1 = to_bytevec(payload.substr(0, 20));
        auto seg2 = to_bytevec(payload.substr(20));

        first_bytes.insert(first_bytes.end(), seg1.begin(), seg1.end());
        last_bytes.insert(last_bytes.end(), seg2.begin(), seg2.end());

        ::rlc_rx_submit(fx.get(), buf::create(last_bytes).strong());
        ::rlc_rx_submit(fx.get(), buf::create(first_bytes).strong());

        REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_DONE).payload ==
               to_bytevec(payload));
        REQUIRE(events.empty());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("UM RX discards a PDU with a SN the reassembly window has passed",
         "[um][rx]")
{
        /* Spec 5.2.2.2.2 second bullet: discard when (RX_Next_Highest -
         * UM_Window_Size) <= SN < RX_Next_Reassembly. Per 7.1 that
         * comparison takes RX_Next_Highest - UM_Window_Size as its modulus
         * base, so with both state variables at 0 and a 12 bit SN the base
         * is 2048 and the range covers every SN from 2048 to the wrap.
         * Note this is not a case of falling outside the reassembly
         * window: 5.2.2.2.1 places that window at [2048, 4096) here, and
         * the SN below is inside it. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_um(fx, back, events) == 0);

        auto w = proto::snwidth::W12;

        /* 3000 falls in the discard range described above. The entity
         * itself never gets that far: um_conf sets window_size to 10,
         * which is not one of the two UM_Window_Size values 7.2 allows
         * (32 for a 6 bit SN, 2048 for a 12 bit SN), so rlc_rx_submit
         * drops the PDU against its own narrower window. Either way
         * nothing may be delivered. */
        proto::um::header hdr{proto::seginfo::FIRST, 3000, std::nullopt};
        auto bytes = hdr.encode(w);
        auto payload = to_bytevec(std::string("unreachable"));
        bytes.insert(bytes.end(), payload.begin(), payload.end());

        ::rlc_rx_submit(fx.get(), buf::create(bytes).strong());

        REQUIRE(events.empty());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("UM RX discards a duplicate PDU segment", "[um][rx]")
{
        /* Spec 5.2.2.2.3: an SDU is reassembled and delivered once "all
         * byte segments with SN = x are received", so re-receiving a
         * segment must not complete it. Note UM has no explicit
         * discard-the-duplicate rule - that is 5.2.3.2.2, an AM clause -
         * so this pins reassembly-level behaviour, not a UM requirement.
         * Uses a segmented SDU (SI=FIRST) rather than SI=ALL so
         * the duplicate is caught by rlc_seg_buf_insert rather than the
         * SDU already having been removed by the first delivery attempt. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_um(fx, back, events) == 0);

        auto w = proto::snwidth::W12;

        proto::um::header hdr{proto::seginfo::FIRST, 0, std::nullopt};
        auto bytes = hdr.encode(w);
        auto payload = to_bytevec(std::string("no duplicates"));
        bytes.insert(bytes.end(), payload.begin(), payload.end());

        auto pdu = buf::create(bytes);

        ::rlc_rx_submit(fx.get(), pdu.strong());
        ::rlc_rx_submit(fx.get(), pdu.strong());

        REQUIRE(events.empty());
        REQUIRE(::rlc_window_base(&fx.get()->rx.win) == 0);

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("UM RX drops an incomplete SDU and advances the window when "
         "t-Reassembly expires",
         "[um][rx]")
{
        /* Spec 5.2.2.2.4/5.2.2.2.3: on t-Reassembly expiry, RX_Next_Highest
         * is used to advance past whatever hasn't completed. */
        gabs_override::timer_ctx timer_ctx(gabs_override::manual_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_um(fx, back, events) == 0);

        auto w = proto::snwidth::W12;

        /* First and last segments of SN=0 with a gap in between, so the
         * SDU is detectably incomplete (rlc_sdu_loss_detected) and
         * t-Reassembly starts - a single FIRST segment on its own is
         * ordinary in-progress reassembly, not evidence of loss. */
        proto::um::header first{proto::seginfo::FIRST, 0, std::nullopt};
        auto first_bytes = first.encode(w);
        auto seg1 = to_bytevec(std::string("first"));
        first_bytes.insert(first_bytes.end(), seg1.begin(), seg1.end());

        proto::um::header last{proto::seginfo::LAST, 0,
                               std::optional<std::uint16_t>(10)};
        auto last_bytes = last.encode(w);
        auto seg2 = to_bytevec(std::string("last!"));
        last_bytes.insert(last_bytes.end(), seg2.begin(), seg2.end());

        ::rlc_rx_submit(fx.get(), buf::create(first_bytes).strong());
        ::rlc_rx_submit(fx.get(), buf::create(last_bytes).strong());
        REQUIRE(events.empty());

        REQUIRE(gabs_override::armed(fx.get()->rx.t_reassembly.gtimer) ==
               true);
        gabs_override::fire(fx.get()->rx.t_reassembly.gtimer);

        REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_FAIL).sn == 0);
        REQUIRE(events.empty());
        REQUIRE(::rlc_window_base(&fx.get()->rx.win) == 1);

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("UM TX segments an SDU across multiple PDUs", "[um][tx]")
{
        /* Spec 4.2.1.2.2: the transmitting entity segments the RLC SDUs,
         * if needed, so that the UMD PDUs fit within the size indicated by
         * the lower layer. The header of each is fixed by 6.2.2.3 and
         * Table 6.2.3.4-1, checked per PDU below. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_um(fx, back, events) == 0);

        std::string content(30, 'x');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(fx.get(), sdu, nullptr) == 0);

        auto w = proto::snwidth::W12;
        std::vector<std::byte> reassembled;

        while (reassembled.size() < content.size()) {
                (void)::rlc_tx_avail(fx.get(), 12);
                REQUIRE(!tx_queue.empty());

                auto bytes = tx_queue.front().vec();
                auto it = bytes.cbegin();
                auto hdr = proto::um::header::decode(it, w);

                auto offset = reassembled.size();
                reassembled.insert(reassembled.end(), it, bytes.cend());
                bool is_last = reassembled.size() == content.size();

                REQUIRE(hdr.sn.value() == 0);

                if (offset == 0) {
                        /* 6.2.2.3: the SN is carried because the SDU is
                         * segmented, and "an UMD PDU carrying the first
                         * segment of an RLC SDU does not carry the SO field
                         * in its header". */
                        REQUIRE(hdr.si == proto::seginfo::FIRST);
                        REQUIRE(hdr.so.has_value() == false);
                } else {
                        /* Table 6.2.3.4-1: 10 is the last segment, 11 one
                         * that is neither first nor last. */
                        REQUIRE(hdr.si == (is_last ? proto::seginfo::LAST
                                                   : proto::seginfo::NEITHER));
                        REQUIRE(hdr.so.value() == offset);
                }

                tx_queue.pop();
        }

        REQUIRE(reassembled == to_bytevec(content));

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("UM TX omits the SN when a segment fills the entire SDU",
         "[um][tx]")
{
        /* Spec 6.2.2.3: "When an UMD PDU contains a complete RLC SDU, the
         * UMD PDU header only contains the SI and R fields" - Figure
         * 6.2.2.3-1 makes that header exactly one octet, against the four
         * of Figure 6.2.2.3-5 (12 bit SN plus a 16 bit SO). The header
         * size is normative, so the transmit opportunity below is the SDU
         * plus exactly one byte: the smallest grant for which the spec
         * still requires a single PDU with SI=ALL. Per 5.2.2.1.1 the SN is
         * set only when the PDU carries a segment. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        std::queue<buf::pbuf_ptr> tx_queue;
        unsigned int tx_cnt = 0;
        backend::backend back(backend::queue_submitter(tx_queue),
                              backend::request_counter(tx_cnt));

        fixture::rlc_ctx fx;
        event::event_handler events;
        REQUIRE(init_um(fx, back, events) == 0);

        auto sdu = buf::create(std::string("fits in one PDU"));
        REQUIRE(::rlc_tx(fx.get(), sdu, nullptr) == 0);

        (void)::rlc_tx_avail(fx.get(), gabs_pbuf_size(sdu) + 1);
        REQUIRE(tx_queue.size() == 1);

        auto w = proto::snwidth::W12;
        auto bytes = tx_queue.front().vec();
        auto it = bytes.cbegin();
        auto hdr = proto::um::header::decode(it, w);

        REQUIRE(hdr.si == proto::seginfo::ALL);
        REQUIRE(hdr.sn.has_value() == false);
        REQUIRE(hdr.so.has_value() == false);
        REQUIRE(std::vector<std::byte>(it, bytes.cend()) == sdu.vec());

        REQUIRE(::rlc_deinit(fx.get()) == 0);
}

TEST_CASE("UM peers exchange a complete SDU end-to-end", "[um][loopback]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx peer_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_a;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_um(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_um(peer_b, backend_b, events_b) == 0);

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

TEST_CASE("UM peers permanently lose an SDU when a segment is dropped",
         "[um][loopback]")
{
        /* Spec 5.3.1: ARQ procedures are only performed by an AM RLC
         * entity - unlike the AM loopback tests, there is no
         * poll/STATUS/NACK cycle to recover a lost segment. The receiver
         * only finds out via t-Reassembly expiry, and the SDU is dropped,
         * not retried. Runs with the loss on each link in turn. */
        bool a_sends = GENERATE(true, false);

        gabs_override::timer_ctx timer_ctx(gabs_override::manual_resolver);

        fixture::rlc_ctx peer_a;
        fixture::rlc_ctx peer_b;
        event::event_handler events_a;
        event::event_handler events_b;

        peer_link link_a{peer_a.get(), peer_b.get()};
        peer_link link_b{peer_b.get(), peer_a.get()};

        (a_sends ? link_a : link_b).drop = drop_first();

        auto backend_a = make_peer_backend(link_a);
        auto backend_b = make_peer_backend(link_b);

        REQUIRE(init_um(peer_a, backend_a, events_a) == 0);
        REQUIRE(init_um(peer_b, backend_b, events_b) == 0);

        auto &sender = a_sends ? peer_a : peer_b;
        auto &receiver = a_sends ? peer_b : peer_a;
        auto &receiver_events = a_sends ? events_b : events_a;

        std::string content(50, 'y');
        auto sdu = buf::create(content);
        REQUIRE(::rlc_tx(sender.get(), sdu, nullptr) == 0);

        pump(link_a, link_b, 20);

        REQUIRE(receiver_events.empty());

        REQUIRE(gabs_override::armed(receiver.get()->rx.t_reassembly.gtimer) ==
               true);
        gabs_override::fire(receiver.get()->rx.t_reassembly.gtimer);

        (void)receiver_events.pop(::rlc_event::RLC_EVENT_RX_FAIL);
        REQUIRE(receiver_events.empty());

        pump(link_a, link_b, 20);

        /* No retransmission ever happens - the drop is permanent. */
        REQUIRE(receiver_events.empty());

        REQUIRE(::rlc_deinit(peer_a.get()) == 0);
        REQUIRE(::rlc_deinit(peer_b.get()) == 0);
}

}; // namespace rlc::test
