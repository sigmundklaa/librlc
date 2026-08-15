
#include <cerrno>
#include <cstdint>
#include <queue>
#include <string>

#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>
#include <rlc/seg_buf.h>

#include "util/mem.hh"
#include "util/buf.hh"
#include "util/backend.hh"
#include "util/event.hh"
#include "util/fake_sdu.hh"
#include "util/fixture.hh"

#include "gabs-overrides/timer/timer.hh"

extern "C" {
#include "../src/arq.c"
}

namespace rlc::test
{

using namespace util;

namespace
{
} // namespace

TEST_CASE("highest_sn_submitted", "[arq][static]")
{
        ::rlc_context ctx = {};
        ::rlc_list_init(&ctx.tx.sdus);
        ctx.alloc_misc = mem::alloc;

        SECTION("no submitted SDUs returns null")
        {
                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                REQUIRE(highest_sn_submitted(&ctx) == nullptr);
        }

        SECTION("returns the only submitted SDU")
        {
                fake_sdu sdu(&ctx, 3, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{5, 10},
                                                  mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                REQUIRE(highest_sn_submitted(&ctx) == sdu.get());
        }

        SECTION("returns the highest SN among submitted SDUs")
        {
                fake_sdu low(&ctx, 1, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&low.get()->tx.unsent,
                                                  ::rlc_seg{5, 10},
                                                  mem::alloc) == 0);

                fake_sdu high(&ctx, 2, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&high.get()->tx.unsent,
                                                  ::rlc_seg{5, 10},
                                                  mem::alloc) == 0);

                fake_sdu unsubmitted(&ctx, 3, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&unsubmitted.get()->tx.unsent,
                                                  ::rlc_seg{0, 10},
                                                  mem::alloc) == 0);

                ::rlc_sdu_queue_insert(&ctx.tx.sdus, low.strong());
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, high.strong());
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, unsubmitted.strong());

                REQUIRE(highest_sn_submitted(&ctx) == high.get());
        }

        ::rlc_sdu_queue_clear(&ctx.tx.sdus);
}

TEST_CASE("last_segment", "[arq][static]")
{
        ::rlc_seg_list list;
        ::rlc_list_init(&list);

        SECTION("single segment is its own last")
        {
                REQUIRE(::rlc_seg_list_insert_all(&list, ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);

                auto seg = last_segment(&list);
                REQUIRE(seg != nullptr);
                REQUIRE(seg->seg.start == 0);
                REQUIRE(seg->seg.end == 5);

                ::rlc_seg_list_clear(&list, mem::alloc);
        }

        SECTION("returns the last of several segments")
        {
                REQUIRE(::rlc_seg_list_insert_all(&list, ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);
                REQUIRE(::rlc_seg_list_insert_all(&list, ::rlc_seg{10, 15},
                                                  mem::alloc) == 0);
                REQUIRE(::rlc_seg_list_insert_all(&list, ::rlc_seg{20, 25},
                                                  mem::alloc) == 0);

                auto seg = last_segment(&list);
                REQUIRE(seg != nullptr);
                REQUIRE(seg->seg.start == 20);
                REQUIRE(seg->seg.end == 25);

                ::rlc_seg_list_clear(&list, mem::alloc);
        }
}

TEST_CASE("adjust_poll_sn", "[arq][static]")
{
        /* Spec 5.3.3.2: set POLL_SN to the highest SN of the AMD PDUs
         * submitted to lower layer. */
        ::rlc_context ctx = {};
        ::rlc_list_init(&ctx.tx.sdus);
        ctx.alloc_misc = mem::alloc;

        SECTION("raises poll_sn to the highest submitted SN")
        {
                fake_sdu low(&ctx, 1, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&low.get()->tx.unsent,
                                                  ::rlc_seg{5, 10},
                                                  mem::alloc) == 0);

                fake_sdu high(&ctx, 4, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&high.get()->tx.unsent,
                                                  ::rlc_seg{5, 10},
                                                  mem::alloc) == 0);

                ::rlc_sdu_queue_insert(&ctx.tx.sdus, low.strong());
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, high.strong());

                adjust_poll_sn(&ctx);

                REQUIRE(ctx.arq.poll_sn == 4);
        }

        SECTION("ignores SDUs that have not been submitted")
        {
                fake_sdu unsubmitted(&ctx, 9, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&unsubmitted.get()->tx.unsent,
                                                  ::rlc_seg{0, 10},
                                                  mem::alloc) == 0);

                ::rlc_sdu_queue_insert(&ctx.tx.sdus, unsubmitted.strong());
                ctx.arq.poll_sn = 2;

                adjust_poll_sn(&ctx);

                REQUIRE(ctx.arq.poll_sn == 2);
        }

        SECTION("never decreases an already higher poll_sn")
        {
                /* 5.3.3.2 says to set POLL_SN, not to take a maximum. The
                 * two agree while SNs advance, so this covers defensive
                 * behaviour, not a requirement. */
                fake_sdu sdu(&ctx, 1, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{5, 10},
                                                  mem::alloc) == 0);

                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());
                ctx.arq.poll_sn = 9;

                adjust_poll_sn(&ctx);

                REQUIRE(ctx.arq.poll_sn == 9);
        }

        ::rlc_sdu_queue_clear(&ctx.tx.sdus);
}

TEST_CASE("tx_pollable", "[arq][static]")
{
        /* Spec 5.3.3.2: conditions for including a poll in an AMD PDU. */
        static const ::rlc_config am_conf = {
                .type = RLC_AM,
                .pdu_without_poll_max = 4,
                .byte_without_poll_max = 100,
        };
        static const ::rlc_config um_conf = {
                .type = RLC_UM,
        };

        ::rlc_context ctx = {};
        ctx.conf = &am_conf;
        ctx.alloc_misc = mem::alloc;

        SECTION("never polls outside AM")
        {
                ctx.conf = &um_conf;
                ctx.arq.force_poll = true;

                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);

                REQUIRE(tx_pollable(&ctx, sdu.get()) == false);
        }

        SECTION("force_poll always polls")
        {
                ctx.arq.force_poll = true;

                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);

                REQUIRE(tx_pollable(&ctx, sdu.get()) == true);
        }

        SECTION("PDU count threshold polls")
        {
                ctx.arq.pdu_without_poll = 4;

                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);

                REQUIRE(tx_pollable(&ctx, sdu.get()) == true);
        }

        SECTION("byte count threshold polls")
        {
                ctx.arq.byte_without_poll = 100;

                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);

                REQUIRE(tx_pollable(&ctx, sdu.get()) == true);
        }

        SECTION("more unsent segments after this one skips poll")
        {
                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{5, 10},
                                                  mem::alloc) == 0);

                REQUIRE(tx_pollable(&ctx, sdu.get()) == false);
        }

        SECTION("bytes left in the single unsent segment skips poll")
        {
                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);

                REQUIRE(tx_pollable(&ctx, sdu.get()) == false);
        }

        SECTION("last segment fully consumed polls")
        {
                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);

                auto it = ::rlc_list_it_init(&sdu.get()->tx.unsent);
                auto item = ::rlc_seg_item_from_it(it);
                item->seg.start = item->seg.end;

                REQUIRE(tx_pollable(&ctx, sdu.get()) == true);
        }
}

TEST_CASE("tx_win_shift", "[arq][static]")
{
        /* Spec 5.2.3.1: the transmitting window's lower edge tracks
         * TX_Next_Ack. */
        ::rlc_context ctx = {};
        ::rlc_list_init(&ctx.tx.sdus);
        ::rlc_window_init(&ctx.tx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;

        SECTION("empty queue shifts to TX_Next")
        {
                ctx.tx.next_sn = 7;

                tx_win_shift(&ctx);

                REQUIRE(::rlc_window_base(&ctx.tx.win) == 7);
        }

        SECTION("non-empty queue shifts to the head SDU's SN")
        {
                ctx.tx.next_sn = 7;

                fake_sdu sdu(&ctx, 3, RLC_READY, true);
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                tx_win_shift(&ctx);

                REQUIRE(::rlc_window_base(&ctx.tx.win) == 3);
        }

        ::rlc_sdu_queue_clear(&ctx.tx.sdus);
}

TEST_CASE("restart_status_prohibit", "[arq][static]")
{
        /* Spec 5.3.4: t-StatusProhibit is (re)started whenever a STATUS
         * PDU is submitted. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        static const ::rlc_config conf = {
                .time_status_prohibit_us = 5000000,
        };

        ::rlc_context ctx = {};
        ctx.conf = &conf;
        REQUIRE(::gabs_timer_ctx_init(&ctx.timer_ctx) == 0);
        REQUIRE(::rlc_timer_install(&ctx.arq.t_status_prohibit,
                                    alarm_status_prohibit, &ctx) == 0);

        SECTION("starts t-StatusProhibit and marks it running")
        {
                REQUIRE(restart_status_prohibit(&ctx) == 0);

                REQUIRE(ctx.arq.status_prohibit == true);
                REQUIRE(gabs_override::armed(
                               ctx.arq.t_status_prohibit.gtimer) == true);
        }

        REQUIRE(::rlc_timer_uninstall(&ctx.arq.t_status_prohibit) == 0);
        REQUIRE(::gabs_timer_ctx_deinit(&ctx.timer_ctx) == 0);
}

TEST_CASE("alarm_poll_retransmit", "[arq][static]")
{
        /* Spec 5.3.3.4: expiry of t-PollRetransmit requests a poll be
         * (re)sent at the next transmit opportunity. */
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));

        ::rlc_context ctx = {};
        ctx.backend = back;
        ctx.alloc_misc = mem::alloc;
        REQUIRE(::rlc_sched_init(&ctx.sched) == 0);

        SECTION("sets force_poll and requests a transmit opportunity")
        {
                alarm_poll_retransmit(&ctx.arq.t_poll_retransmit, &ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(ctx.arq.force_poll == true);
                REQUIRE(cnt == 1);
        }

        REQUIRE(::rlc_sched_deinit(&ctx.sched) == 0);
}

TEST_CASE("alarm_status_prohibit", "[arq][static]")
{
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));

        ::rlc_context ctx = {};
        ctx.backend = back;
        ctx.alloc_misc = mem::alloc;
        ctx.arq.status_prohibit = true;
        REQUIRE(::rlc_sched_init(&ctx.sched) == 0);

        SECTION("clears status_prohibit and requests a transmit opportunity")
        {
                alarm_status_prohibit(&ctx.arq.t_status_prohibit, &ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(ctx.arq.status_prohibit == false);
                REQUIRE(cnt == 1);
        }

        REQUIRE(::rlc_sched_deinit(&ctx.sched) == 0);
}

TEST_CASE("tx_ack", "[arq][static]")
{
        /* Spec 5.2.3.1: positive acknowledgement advances TX_Next_Ack past
         * every RLC SDU up to (not including) ACK_SN. */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx fx;
        ::rlc_context &ctx = *fx.get();
        event::event_handler events;

        ::rlc_list_init(&ctx.tx.sdus);
        ::rlc_window_init(&ctx.tx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;
        fx.on_event(events.listener());
        ctx.listener = fixture::rlc_ctx::listener_trampoline;
        REQUIRE(::rlc_sched_init(&ctx.sched) == 0);

        /* Spec 5.3.3.3: an ack covering POLL_SN stops t-PollRetransmit, so
         * tx_ack needs the timer even where only releases are asserted. */
        REQUIRE(::gabs_timer_ctx_init(&ctx.timer_ctx) == 0);
        REQUIRE(::rlc_timer_install(&ctx.arq.t_poll_retransmit,
                                    alarm_poll_retransmit, &ctx) == 0);

        SECTION("acks a contiguous sent prefix, shifting window as it goes")
        {
                fake_sdu sdu0(&ctx, 0, RLC_WAIT, true);
                fake_sdu sdu1(&ctx, 1, RLC_WAIT, true);
                fake_sdu sdu2(&ctx, 2, RLC_READY, true);

                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu0.strong());
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu1.strong());
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu2.strong());

                tx_ack(&ctx, 2);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(events.pop(::rlc_event::RLC_EVENT_TX_RELEASE).sn ==
                       0);
                REQUIRE(events.pop(::rlc_event::RLC_EVENT_TX_RELEASE).sn ==
                       1);
                REQUIRE(events.empty());

                REQUIRE(::rlc_window_base(&ctx.tx.win) == 2);
                REQUIRE(::rlc_sdu_queue_get(&ctx.tx.sdus, 2) == sdu2.get());
        }

        SECTION("stops at the ack boundary even if more SDUs were sent")
        {
                fake_sdu sdu0(&ctx, 0, RLC_WAIT, true);
                fake_sdu sdu1(&ctx, 1, RLC_WAIT, true);

                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu0.strong());
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu1.strong());

                tx_ack(&ctx, 1);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(events.pop(::rlc_event::RLC_EVENT_TX_RELEASE).sn ==
                       0);
                REQUIRE(events.empty());

                REQUIRE(::rlc_sdu_queue_get(&ctx.tx.sdus, 1) == sdu1.get());
        }

        SECTION("stops at an SDU still awaiting its first transmission")
        {
                fake_sdu sdu(&ctx, 0, RLC_READY, true);

                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                tx_ack(&ctx, 5);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(events.empty());
                REQUIRE(::rlc_window_base(&ctx.tx.win) == 0);
        }

        REQUIRE(::rlc_timer_uninstall(&ctx.arq.t_poll_retransmit) == 0);
        REQUIRE(::gabs_timer_ctx_deinit(&ctx.timer_ctx) == 0);
        REQUIRE(::rlc_sched_deinit(&ctx.sched) == 0);

        ::rlc_sdu_queue_clear(&ctx.tx.sdus);
}

TEST_CASE("tx_nack_clear", "[arq][static]")
{
        ::rlc_context ctx = {};
        ::rlc_list_init(&ctx.tx.sdus);
        ctx.alloc_misc = mem::alloc;

        SECTION("trims a below-SN SDU's unsent list to its last segment")
        {
                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{10, 15},
                                                  mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                tx_nack_clear(&ctx, 1);

                auto it = ::rlc_list_it_init(&sdu.get()->tx.unsent);
                auto item = ::rlc_seg_item_from_it(it);
                REQUIRE(item != nullptr);
                REQUIRE(item->seg.start == 10);
                REQUIRE(item->seg.end == 15);
                REQUIRE(::rlc_list_it_eoi(::rlc_list_it_next(it)) == true);
        }

        SECTION("leaves SDUs at or above the SN untouched")
        {
                fake_sdu sdu(&ctx, 3, RLC_READY, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{10, 15},
                                                  mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                tx_nack_clear(&ctx, 1);

                auto it = ::rlc_list_it_init(&sdu.get()->tx.unsent);
                auto item = ::rlc_seg_item_from_it(it);
                REQUIRE(item != nullptr);
                REQUIRE(item->seg.start == 0);
                REQUIRE(item->seg.end == 5);
                REQUIRE(::rlc_list_it_eoi(::rlc_list_it_next(it)) == false);
        }

        ::rlc_sdu_queue_clear(&ctx.tx.sdus);
}

TEST_CASE("retransmit_sdu", "[arq][static]")
{
        /* Spec 5.3.2: RETX_COUNT bookkeeping and maxRetxThreshold. */
        static const ::rlc_config conf = {
                .max_retx_threshhold = 3,
        };

        fixture::rlc_ctx fx;
        ::rlc_context &ctx = *fx.get();
        event::event_handler events;
        ctx.conf = &conf;

        ::rlc_list_init(&ctx.tx.sdus);
        ::rlc_window_init(&ctx.tx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;
        fx.on_event(events.listener());
        ctx.listener = fixture::rlc_ctx::listener_trampoline;
        REQUIRE(::rlc_sched_init(&ctx.sched) == 0);

        SECTION("marks a not-yet-pending SDU for retransmission")
        {
                /* Spec 5.3.2 counts a first retransmission as zero; arq.c
                 * counts from one and raises the threshold to match, so
                 * this counts retransmissions, not the spec's variable. */
                fake_sdu sdu(&ctx, 0, RLC_WAIT, true);
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                ::rlc_seg seg{0, 5};
                REQUIRE(retransmit_sdu(&ctx, sdu.get(), &seg) == true);

                REQUIRE(sdu.get()->state == RLC_READY);
                REQUIRE(sdu.get()->tx.retx_count == 1);
        }

        SECTION("already pending does not increment RETX_COUNT again")
        {
                fake_sdu sdu(&ctx, 0, RLC_READY, true);
                sdu.get()->tx.retx_count = 1;
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                ::rlc_seg seg{5, 10};
                REQUIRE(retransmit_sdu(&ctx, sdu.get(), &seg) == true);

                REQUIRE(sdu.get()->state == RLC_READY);
                REQUIRE(sdu.get()->tx.retx_count == 1);
        }

        SECTION("a fully duplicate segment is a no-op besides state")
        {
                fake_sdu sdu(&ctx, 0, RLC_WAIT, true);
                REQUIRE(::rlc_seg_list_insert_all(&sdu.get()->tx.unsent,
                                                  ::rlc_seg{0, 5},
                                                  mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                ::rlc_seg seg{0, 5};
                REQUIRE(retransmit_sdu(&ctx, sdu.get(), &seg) == true);

                REQUIRE(sdu.get()->state == RLC_READY);
                REQUIRE(sdu.get()->tx.retx_count == 0);
        }

        SECTION("survives maxRetxThreshold retransmissions, then fails")
        {
                /* Spec 5.3.2: reaching maxRetxThreshold reports the
                 * failure, so exactly that many retransmissions come
                 * first. Counted in calls so it holds whichever value
                 * arq.c counts from. */
                ctx.tx.next_sn = 1;

                fake_sdu sdu(&ctx, 0, RLC_WAIT, true);
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                for (std::uint32_t i = 0; i < conf.max_retx_threshhold; i++) {
                        ::rlc_seg seg{0, 5};
                        REQUIRE(retransmit_sdu(&ctx, sdu.get(), &seg) == true);

                        /* Re-arm the SDU the way a completed retransmission
                         * would: unsent list drained, awaiting ack again. */
                        ::rlc_seg_list_clear(&sdu.get()->tx.unsent, mem::alloc);
                        sdu.get()->state = RLC_WAIT;
                }

                ::rlc_seg seg{0, 5};
                REQUIRE(retransmit_sdu(&ctx, sdu.get(), &seg) == false);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(::rlc_sdu_queue_get(&ctx.tx.sdus, 0) == nullptr);
                REQUIRE(::rlc_window_base(&ctx.tx.win) == 1);
                REQUIRE(events.pop(::rlc_event::RLC_EVENT_TX_RELEASE).sn ==
                       0);
                REQUIRE(events.empty());
        }

        REQUIRE(::rlc_sched_deinit(&ctx.sched) == 0);

        ::rlc_sdu_queue_clear(&ctx.tx.sdus);
}

TEST_CASE("process_nack", "[arq][static]")
{
        /* Spec 5.3.2: a bare NACK_SN retransmits the whole RLC SDU. */
        static const ::rlc_config conf = {
                .max_retx_threshhold = 3,
        };

        ::rlc_context ctx = {};
        ctx.conf = &conf;
        ::rlc_list_init(&ctx.tx.sdus);
        ::rlc_window_init(&ctx.tx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;

        SECTION("queues the whole SDU buffer for retransmission")
        {
                fake_sdu sdu(&ctx, 4, RLC_WAIT, true);
                sdu.get()->tx.buffer = buf::create(std::string(10, 'x')).strong();
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                ::rlc_pdu_status cur = {};
                cur.nack_sn = 4;

                process_nack(&ctx, &cur);

                REQUIRE(sdu.get()->state == RLC_READY);

                auto it = ::rlc_list_it_init(&sdu.get()->tx.unsent);
                auto item = ::rlc_seg_item_from_it(it);
                REQUIRE(item != nullptr);
                REQUIRE(item->seg.start == 0);
                REQUIRE(item->seg.end == 10);
        }

        SECTION("an unknown SN is a no-op")
        {
                ::rlc_pdu_status cur = {};
                cur.nack_sn = 99;

                process_nack(&ctx, &cur);

                REQUIRE(::rlc_sdu_queue_get(&ctx.tx.sdus, 99) == nullptr);
        }

        SECTION("a NACK matching POLL_SN stops t-PollRetransmit")
        {
                /* Spec 5.3.3.3: the ack for POLL_SN may be negative.
                 * rlc_arq_rx_status routes a bare NACK_SN here. */
                gabs_override::timer_ctx timer_ctx(
                        gabs_override::default_resolver);
                REQUIRE(::gabs_timer_ctx_init(&ctx.timer_ctx) == 0);
                REQUIRE(::rlc_timer_install(&ctx.arq.t_poll_retransmit,
                                            alarm_poll_retransmit, &ctx) ==
                       0);
                REQUIRE(::rlc_timer_start(&ctx.arq.t_poll_retransmit,
                                          5000000) == 0);

                fake_sdu sdu(&ctx, 4, RLC_WAIT, true);
                sdu.get()->tx.buffer = buf::create(std::string(10, 'x')).strong();
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());
                ctx.arq.poll_sn = 4;

                ::rlc_pdu_status cur = {};
                cur.nack_sn = 4;

                process_nack(&ctx, &cur);

                /* Tear down before asserting: a failing REQUIRE unwinds
                 * out of the SECTION and would leak. */
                auto still_armed =
                        gabs_override::armed(ctx.arq.t_poll_retransmit.gtimer);

                REQUIRE(::rlc_timer_uninstall(&ctx.arq.t_poll_retransmit) ==
                       0);
                REQUIRE(::gabs_timer_ctx_deinit(&ctx.timer_ctx) == 0);

                REQUIRE(still_armed == false);
        }

        ::rlc_sdu_queue_clear(&ctx.tx.sdus);
}

TEST_CASE("process_nack_offset", "[arq][static]")
{
        /* Spec 5.3.2: NACK_SN with SOstart/SOend retransmits only that
         * byte range. */
        static const ::rlc_config conf = {
                .max_retx_threshhold = 3,
        };

        ::rlc_context ctx = {};
        ctx.conf = &conf;
        ::rlc_list_init(&ctx.tx.sdus);
        ::rlc_window_init(&ctx.tx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;

        SECTION("retransmits only the NACKed byte range")
        {
                fake_sdu sdu(&ctx, 4, RLC_WAIT, true);
                sdu.get()->tx.buffer = buf::create(std::string(10, 'x')).strong();
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                ::rlc_pdu_status cur = {};
                cur.nack_sn = 4;
                cur.ext.has_offset = true;
                cur.offset = ::rlc_seg{2, 5};

                process_nack_offset(&ctx, &cur);

                REQUIRE(sdu.get()->state == RLC_READY);

                auto it = ::rlc_list_it_init(&sdu.get()->tx.unsent);
                auto item = ::rlc_seg_item_from_it(it);
                REQUIRE(item != nullptr);
                REQUIRE(item->seg.start == 2);
                REQUIRE(item->seg.end == 5);
        }

        SECTION("a max SOend resolves to the SDU buffer size")
        {
                fake_sdu sdu(&ctx, 4, RLC_WAIT, true);
                sdu.get()->tx.buffer = buf::create(std::string(10, 'x')).strong();
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());

                ::rlc_pdu_status cur = {};
                cur.nack_sn = 4;
                cur.ext.has_offset = true;
                cur.offset = ::rlc_seg{2, RLC_STATUS_SO_MAX};

                process_nack_offset(&ctx, &cur);

                auto it = ::rlc_list_it_init(&sdu.get()->tx.unsent);
                auto item = ::rlc_seg_item_from_it(it);
                REQUIRE(item != nullptr);
                REQUIRE(item->seg.start == 2);
                REQUIRE(item->seg.end == 10);
        }

        SECTION("a NACK matching POLL_SN stops t-PollRetransmit")
        {
                /* Spec 5.3.3.3: an ack for SN == POLL_SN, positive or
                 * negative, stops and resets t-PollRetransmit.
                 * rlc_arq_rx_status only routes here with an offset. */
                gabs_override::timer_ctx timer_ctx(
                        gabs_override::default_resolver);
                REQUIRE(::gabs_timer_ctx_init(&ctx.timer_ctx) == 0);
                REQUIRE(::rlc_timer_install(&ctx.arq.t_poll_retransmit,
                                            alarm_poll_retransmit, &ctx) ==
                       0);
                REQUIRE(::rlc_timer_start(&ctx.arq.t_poll_retransmit,
                                          5000000) == 0);

                fake_sdu sdu(&ctx, 4, RLC_WAIT, true);
                sdu.get()->tx.buffer = buf::create(std::string(10, 'x')).strong();
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu.strong());
                ctx.arq.poll_sn = 4;

                ::rlc_pdu_status cur = {};
                cur.nack_sn = 4;
                cur.ext.has_offset = true;
                cur.offset = ::rlc_seg{0, 5};

                process_nack_offset(&ctx, &cur);

                REQUIRE(gabs_override::armed(
                               ctx.arq.t_poll_retransmit.gtimer) == false);

                REQUIRE(::rlc_timer_uninstall(&ctx.arq.t_poll_retransmit) ==
                       0);
                REQUIRE(::gabs_timer_ctx_deinit(&ctx.timer_ctx) == 0);
        }

        ::rlc_sdu_queue_clear(&ctx.tx.sdus);
}

TEST_CASE("process_nack_range", "[arq][static]")
{
        /* Spec 5.3.2: NACK_SN with a NACK range retransmits every RLC SDU
         * in that range. */
        static const ::rlc_config conf = {
                .max_retx_threshhold = 2,
        };

        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        fixture::rlc_ctx fx;
        ::rlc_context &ctx = *fx.get();
        event::event_handler events;
        ctx.conf = &conf;

        ::rlc_list_init(&ctx.tx.sdus);
        ::rlc_window_init(&ctx.tx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;
        fx.on_event(events.listener());
        ctx.listener = fixture::rlc_ctx::listener_trampoline;
        REQUIRE(::rlc_sched_init(&ctx.sched) == 0);

        /* Spec 5.3.3.3: a range covering POLL_SN stops t-PollRetransmit,
         * which any range starting at the default POLL_SN of zero does. */
        REQUIRE(::gabs_timer_ctx_init(&ctx.timer_ctx) == 0);
        REQUIRE(::rlc_timer_install(&ctx.arq.t_poll_retransmit,
                                    alarm_poll_retransmit, &ctx) == 0);

        SECTION("retransmits every SDU within the range, ignores the rest")
        {
                fake_sdu sdu0(&ctx, 0, RLC_WAIT, true);
                sdu0.get()->tx.buffer = buf::create(std::string(4, 'x')).strong();

                fake_sdu sdu1(&ctx, 1, RLC_WAIT, true);
                sdu1.get()->tx.buffer = buf::create(std::string(4, 'x')).strong();

                fake_sdu sdu2(&ctx, 2, RLC_WAIT, true);

                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu0.strong());
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu1.strong());
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu2.strong());

                ::rlc_pdu_status cur = {};
                cur.nack_sn = 0;
                cur.range = 2;

                process_nack_range(&ctx, &cur);

                REQUIRE(sdu0.get()->state == RLC_READY);
                REQUIRE(sdu1.get()->state == RLC_READY);
                REQUIRE(sdu2.get()->state == RLC_WAIT);

                auto it2 = ::rlc_list_it_init(&sdu2.get()->tx.unsent);
                REQUIRE(::rlc_list_it_eoi(it2));
        }

        SECTION("an SDU removed mid-range does not stop later ones")
        {
                ctx.tx.next_sn = 2;

                fake_sdu sdu0(&ctx, 0, RLC_WAIT, true);
                /* Already at the limit, so this NACK is the one that
                 * exhausts it and removes the SDU mid-iteration. */
                sdu0.get()->tx.retx_count = conf.max_retx_threshhold;
                sdu0.get()->tx.buffer = buf::create(std::string(4, 'x')).strong();

                fake_sdu sdu1(&ctx, 1, RLC_WAIT, true);
                sdu1.get()->tx.buffer = buf::create(std::string(4, 'x')).strong();

                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu0.strong());
                ::rlc_sdu_queue_insert(&ctx.tx.sdus, sdu1.strong());

                ::rlc_pdu_status cur = {};
                cur.nack_sn = 0;
                cur.range = 2;

                process_nack_range(&ctx, &cur);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(::rlc_sdu_queue_get(&ctx.tx.sdus, 0) == nullptr);
                REQUIRE(events.pop(::rlc_event::RLC_EVENT_TX_RELEASE).sn ==
                       0);
                REQUIRE(events.empty());

                REQUIRE(sdu1.get()->state == RLC_READY);
                auto it = ::rlc_list_it_init(&sdu1.get()->tx.unsent);
                auto item = ::rlc_seg_item_from_it(it);
                REQUIRE(item != nullptr);
                REQUIRE(item->seg.start == 0);
                REQUIRE(item->seg.end == 4);
        }

        REQUIRE(::rlc_timer_uninstall(&ctx.arq.t_poll_retransmit) == 0);
        REQUIRE(::gabs_timer_ctx_deinit(&ctx.timer_ctx) == 0);
        REQUIRE(::rlc_sched_deinit(&ctx.sched) == 0);

        ::rlc_sdu_queue_clear(&ctx.tx.sdus);
}

TEST_CASE("encode_last", "[arq][static]")
{
        /* Spec 5.3.4: STATUS PDU field construction. */
        static const ::rlc_config conf = {
                .sn_width = RLC_SN_18BIT,
        };

        ::rlc_context ctx = {};
        ctx.conf = &conf;

        SECTION("encodes the pool's last entry and sets its more-bit")
        {
                struct status_pool pool = {};
                pool.index = 1;
                pool.mem[0].nack_sn = 5;

                auto buf = buf::create(RLC_STATUS_MAX_SIZE);
                auto bytes = encode_last(&ctx, &pool, buf);

                REQUIRE(bytes > 0);
                REQUIRE(pool.mem[0].ext.has_more == true);

                ::rlc_pdu_status decoded = {};
                REQUIRE(::rlc_status_decode(&decoded, buf, RLC_SN_18BIT) == 0);
                REQUIRE(decoded.nack_sn == 5);
                REQUIRE(decoded.ext.has_more == true);
        }

        SECTION("returns -ENOSPC when the buffer is too small")
        {
                struct status_pool pool = {};
                pool.index = 1;
                pool.mem[0].nack_sn = 5;

                auto buf = buf::create(1);

                REQUIRE(encode_last(&ctx, &pool, buf) == -ENOSPC);
        }
}

TEST_CASE("create_nack_range", "[arq][static]")
{
        /* Spec 5.3.4: a continuous sequence of missing RLC SDUs is
         * reported as NACK_SN plus a NACK range. */
        static const ::rlc_config conf = {
                .sn_width = RLC_SN_18BIT,
        };

        ::rlc_context ctx = {};
        ctx.conf = &conf;
        ctx.alloc_misc = mem::alloc;

        SECTION("a single missing SDU has no range extension")
        {
                struct status_pool pool = {};
                auto buf = buf::create(RLC_STATUS_MAX_SIZE);

                ::rlc_sdu sdu_next = {};
                sdu_next.sn = 6;

                REQUIRE(create_nack_range(&ctx, &pool, buf, &sdu_next, 5) ==
                       0);
                REQUIRE(encode_last(&ctx, &pool, buf) > 0);

                ::rlc_pdu_status decoded = {};
                REQUIRE(::rlc_status_decode(&decoded, buf, RLC_SN_18BIT) == 0);
                REQUIRE(decoded.nack_sn == 5);
                REQUIRE(decoded.ext.has_range == false);
        }

        SECTION("several missing SDUs set the range extension and count")
        {
                struct status_pool pool = {};
                auto buf = buf::create(RLC_STATUS_MAX_SIZE);

                ::rlc_sdu sdu_next = {};
                sdu_next.sn = 8;

                REQUIRE(create_nack_range(&ctx, &pool, buf, &sdu_next, 5) ==
                       0);
                REQUIRE(encode_last(&ctx, &pool, buf) > 0);

                ::rlc_pdu_status decoded = {};
                REQUIRE(::rlc_status_decode(&decoded, buf, RLC_SN_18BIT) == 0);
                REQUIRE(decoded.nack_sn == 5);
                REQUIRE(decoded.ext.has_range == true);
                REQUIRE(decoded.range == 3);
        }
}

TEST_CASE("create_nack_segment", "[arq][static]")
{
        /* Spec 5.3.4: a continuous sequence of missing byte segments of a
         * partly received RLC SDU is reported as NACK_SN, SOstart, SOend. */
        static const ::rlc_config conf = {
                .sn_width = RLC_SN_18BIT,
        };

        ::rlc_context ctx = {};
        ctx.conf = &conf;
        ctx.alloc_misc = mem::alloc;

        SECTION("stages a NACK for the given byte range")
        {
                struct status_pool pool = {};
                auto buf = buf::create(RLC_STATUS_MAX_SIZE);

                REQUIRE(create_nack_segment(&ctx, &pool, buf, 5,
                                            ::rlc_seg{2, 9}) == 0);
                REQUIRE(encode_last(&ctx, &pool, buf) > 0);

                ::rlc_pdu_status decoded = {};
                REQUIRE(::rlc_status_decode(&decoded, buf, RLC_SN_18BIT) == 0);
                REQUIRE(decoded.nack_sn == 5);
                REQUIRE(decoded.ext.has_offset == true);
                REQUIRE(decoded.offset.start == 2);
                REQUIRE(decoded.offset.end == 9);
        }
}

TEST_CASE("create_nack_offset", "[arq][static]")
{
        /* Spec 5.3.4: reporting the missing byte segments of a partly
         * received RLC SDU. */
        static const ::rlc_config conf = {
                .sn_width = RLC_SN_18BIT,
        };

        ::rlc_context ctx = {};
        ctx.conf = &conf;
        ctx.alloc_misc = mem::alloc;

        SECTION("reports a leading gap before the first received byte")
        {
                struct status_pool pool = {};
                auto buf = buf::create(RLC_STATUS_MAX_SIZE);

                fake_sdu sdu(&ctx, 7, RLC_READY, false);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{5, 10}, mem::alloc,
                                             mem::alloc) == 0);
                sdu.get()->rx.last_received = true;

                create_nack_offset(&ctx, &pool, buf, sdu.get());

                REQUIRE(status_count(&pool) == 1);
                REQUIRE(encode_last(&ctx, &pool, buf) > 0);

                ::rlc_pdu_status decoded = {};
                REQUIRE(::rlc_status_decode(&decoded, buf, RLC_SN_18BIT) == 0);
                REQUIRE(decoded.nack_sn == 7);
                REQUIRE(decoded.offset.start == 0);
                REQUIRE(decoded.offset.end == 5);
        }

        SECTION("reports a trailing gap after the last received byte")
        {
                struct status_pool pool = {};
                auto buf = buf::create(RLC_STATUS_MAX_SIZE);

                fake_sdu sdu(&ctx, 7, RLC_READY, false);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{0, 5}, mem::alloc,
                                             mem::alloc) == 0);
                sdu.get()->rx.last_received = false;

                create_nack_offset(&ctx, &pool, buf, sdu.get());

                REQUIRE(status_count(&pool) == 1);
                REQUIRE(encode_last(&ctx, &pool, buf) > 0);

                ::rlc_pdu_status decoded = {};
                REQUIRE(::rlc_status_decode(&decoded, buf, RLC_SN_18BIT) == 0);
                REQUIRE(decoded.nack_sn == 7);
                REQUIRE(decoded.offset.start == 5);
                REQUIRE(decoded.offset.end == RLC_STATUS_SO_MAX);
        }

        SECTION("reports a gap between two received segments")
        {
                struct status_pool pool = {};
                auto buf = buf::create(RLC_STATUS_MAX_SIZE);

                fake_sdu sdu(&ctx, 7, RLC_READY, false);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{0, 5}, mem::alloc,
                                             mem::alloc) == 0);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{10, 15}, mem::alloc,
                                             mem::alloc) == 0);
                sdu.get()->rx.last_received = true;

                create_nack_offset(&ctx, &pool, buf, sdu.get());

                REQUIRE(status_count(&pool) == 1);
                REQUIRE(encode_last(&ctx, &pool, buf) > 0);

                ::rlc_pdu_status decoded = {};
                REQUIRE(::rlc_status_decode(&decoded, buf, RLC_SN_18BIT) == 0);
                REQUIRE(decoded.nack_sn == 7);
                REQUIRE(decoded.offset.start == 5);
                REQUIRE(decoded.offset.end == 10);
        }

        SECTION("reports both a leading and a trailing gap")
        {
                struct status_pool pool = {};
                auto buf = buf::create(RLC_STATUS_MAX_SIZE);

                fake_sdu sdu(&ctx, 7, RLC_READY, false);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{5, 10}, mem::alloc,
                                             mem::alloc) == 0);
                sdu.get()->rx.last_received = false;

                create_nack_offset(&ctx, &pool, buf, sdu.get());

                REQUIRE(status_count(&pool) == 2);
        }
}
}; // namespace rlc::test
