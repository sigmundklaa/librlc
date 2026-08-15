
#include <cstdint>
#include <string>

#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>
#include <rlc/seg_buf.h>

#include "util/mem.hh"
#include "util/buf.hh"
#include "util/event.hh"
#include "util/fixture.hh"

#include "gabs-overrides/timer/timer.hh"

extern "C" {
#include "../src/rx.c"
}

namespace rlc::test
{

using namespace util;

namespace
{

/* An RX SDU to hand to the code under test. Holds a reference of its own
 * on top of the one the queue takes, so it survives being delivered or
 * dropped, and releases whatever is left in the destructor. */
class fake_sdu
{
      public:
        fake_sdu(::rlc_context *ctx, std::uint32_t sn,
                 enum rlc_sdu_state state)
                : ctx(ctx), sdu(::rlc_sdu_alloc(ctx, false))
        {
                REQUIRE(sdu != nullptr);

                sdu->sn = sn;
                sdu->state = state;

                ::rlc_sdu_incref(sdu);
        }

        fake_sdu(const fake_sdu &) = delete;

        ~fake_sdu()
        {
                /* Still queued means the code under test never took the
                 * queue's reference, and nothing here clears the queue. */
                if (::rlc_sdu_queue_get(&ctx->rx.sdus, sdu->sn) == sdu) {
                        ::rlc_sdu_queue_remove(&ctx->rx.sdus, sdu);
                        ::rlc_sdu_decref(sdu);
                }

                ::rlc_sdu_decref(sdu);
        }

        ::rlc_sdu *get() const
        {
                return sdu;
        }

      private:
        ::rlc_context *ctx;
        ::rlc_sdu *sdu;
};

} // namespace

TEST_CASE("should_start_reassembly", "[rx][static]")
{
        /* Spec 5.2.2.2.3 / 5.2.3.2.3, "if t-Reassembly is not running":
         * start if RX_Next_Highest > base+1, or if RX_Next_Highest == base+1
         * and the SDU at base still has a missing byte segment. */
        ::rlc_context ctx = {};
        ::rlc_list_init(&ctx.rx.sdus);
        ::rlc_window_init(&ctx.rx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;

        SECTION("more than one SDU pending triggers start")
        {
                ctx.rx.next_highest = 5;

                REQUIRE(should_start_reassembly(&ctx) == true);
        }

        SECTION("one pending SDU with a gap triggers start")
        {
                ctx.rx.next_highest = 1;

                fake_sdu sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{0, 3}, mem::alloc,
                                             mem::alloc) == 0);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{5, 8}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu.get());

                REQUIRE(should_start_reassembly(&ctx) == true);
        }

        SECTION("one pending SDU fully contiguous does not trigger start")
        {
                ctx.rx.next_highest = 1;

                fake_sdu sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{0, 5}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu.get());

                REQUIRE(should_start_reassembly(&ctx) == false);
        }

        SECTION("no SDU object at base does not trigger start")
        {
                ctx.rx.next_highest = 1;

                REQUIRE(should_start_reassembly(&ctx) == false);
        }

        SECTION("nothing pending does not trigger start")
        {
                ctx.rx.next_highest = 0;

                REQUIRE(should_start_reassembly(&ctx) == false);
        }
}

TEST_CASE("should_stop_reassembly", "[rx][static]")
{
        /* Spec 5.2.2.2.3 / 5.2.3.2.3, "if t-Reassembly is running". */
        ::rlc_context ctx = {};
        ::rlc_list_init(&ctx.rx.sdus);
        ::rlc_window_init(&ctx.rx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;

        SECTION("trigger at or before window base stops reassembly")
        {
                ctx.rx.next_status_trigger = 0;

                REQUIRE(should_stop_reassembly(&ctx) == true);
        }

        SECTION("trigger one past base, SDU fully received, stops")
        {
                ctx.rx.next_status_trigger = 1;

                fake_sdu sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{0, 5}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu.get());

                REQUIRE(should_stop_reassembly(&ctx) == true);
        }

        SECTION("trigger one past base, SDU still gapped, does not stop")
        {
                ctx.rx.next_status_trigger = 1;

                fake_sdu sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{0, 3}, mem::alloc,
                                             mem::alloc) == 0);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{5, 8}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu.get());

                REQUIRE(should_stop_reassembly(&ctx) == false);
        }

        SECTION("trigger one past base, no SDU object, does not stop")
        {
                ctx.rx.next_status_trigger = 1;

                REQUIRE(should_stop_reassembly(&ctx) == false);
        }

        SECTION("trigger beyond window end should stop")
        {
                /* Per spec 5.2.3.2.3 third bullet, a trigger outside the
                 * receiving window (and not equal to the window end) should
                 * stop t-Reassembly. */
                ctx.rx.next_status_trigger = 15; /* base(0) + width(10) + 5 */

                REQUIRE(should_stop_reassembly(&ctx) == true);
        }
}

TEST_CASE("should_restart_reassembly", "[rx][static]")
{
        /* Spec 5.2.2.2.4 / 5.2.3.2.4, "when t-Reassembly expires" restart
         * condition. */
        ::rlc_context ctx = {};
        ::rlc_list_init(&ctx.rx.sdus);
        ::rlc_window_init(&ctx.rx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;

        SECTION("more than one SDU still pending after expiry restarts")
        {
                ctx.rx.next_highest = 5;

                REQUIRE(should_restart_reassembly(&ctx) == true);
        }

        SECTION("one pending SDU with a gap restarts")
        {
                ctx.rx.next_highest = 1;

                fake_sdu sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{0, 3}, mem::alloc,
                                             mem::alloc) == 0);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{5, 8}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu.get());

                REQUIRE(should_restart_reassembly(&ctx) == true);
        }

        SECTION("one pending SDU fully contiguous does not restart")
        {
                ctx.rx.next_highest = 1;

                fake_sdu sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu.get()->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{0, 5}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu.get());

                REQUIRE(should_restart_reassembly(&ctx) == false);
        }

        SECTION("no SDU object at base does not restart")
        {
                ctx.rx.next_highest = 1;

                REQUIRE(should_restart_reassembly(&ctx) == false);
        }

        SECTION("nothing pending does not restart")
        {
                ctx.rx.next_highest = 0;

                REQUIRE(should_restart_reassembly(&ctx) == false);
        }
}

TEST_CASE("lowest_sn_not_recv", "[rx][static]")
{
        ::rlc_context ctx = {};
        ::rlc_list_init(&ctx.rx.sdus);
        ::rlc_window_init(&ctx.rx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;

        SECTION("empty queue returns RX_Next_Highest")
        {
                ctx.rx.next_highest = 7;

                REQUIRE(lowest_sn_not_recv(&ctx) == 7);
        }

        SECTION("returns the first SN not in the DONE state")
        {
                fake_sdu sdu0(&ctx, 0, RLC_DONE);
                fake_sdu sdu1(&ctx, 1, RLC_DONE);
                fake_sdu sdu2(&ctx, 2, RLC_READY);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2.get());

                REQUIRE(lowest_sn_not_recv(&ctx) == 2);
        }

        SECTION("returns the missing SN at a gap in the sequence")
        {
                fake_sdu sdu0(&ctx, 0, RLC_DONE);
                fake_sdu sdu2(&ctx, 2, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2.get());

                REQUIRE(lowest_sn_not_recv(&ctx) == 1);
        }

        SECTION("all contiguous and done returns RX_Next_Highest")
        {
                fake_sdu sdu0(&ctx, 0, RLC_DONE);
                fake_sdu sdu1(&ctx, 1, RLC_DONE);
                fake_sdu sdu2(&ctx, 2, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2.get());

                ctx.rx.next_highest = 3;

                REQUIRE(lowest_sn_not_recv(&ctx) == 3);
        }
}

TEST_CASE("deliver_ready", "[rx][static]")
{
        fixture::rlc_ctx fx;
        ::rlc_context &ctx = *fx.get();
        event::event_handler events;

        ::rlc_list_init(&ctx.rx.sdus);
        ::rlc_window_init(&ctx.rx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;
        fx.on_event(events.listener());
        ctx.listener = fixture::rlc_ctx::listener_trampoline;
        REQUIRE(::rlc_sched_init(&ctx.sched) == 0);

        SECTION("delivers contiguous DONE prefix in order")
        {
                fake_sdu sdu0(&ctx, 0, RLC_DONE);
                fake_sdu sdu1(&ctx, 1, RLC_DONE);
                fake_sdu sdu2(&ctx, 2, RLC_READY);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2.get());

                deliver_ready(&ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_DONE).sn == 0);
                REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_DONE).sn == 1);
                REQUIRE(events.empty());

                /* sdu2 is not DONE, so it remains queued, undelivered. */
                REQUIRE(::rlc_sdu_queue_get(&ctx.rx.sdus, 2) == sdu2.get());
        }

        SECTION("stops at first gap even if a later SDU is DONE")
        {
                fake_sdu sdu0(&ctx, 0, RLC_DONE);
                fake_sdu sdu2(&ctx, 2, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2.get());

                deliver_ready(&ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_DONE).sn == 0);
                REQUIRE(events.empty());

                REQUIRE(::rlc_sdu_queue_get(&ctx.rx.sdus, 2) == sdu2.get());
        }

        SECTION("nothing ready at the base delivers nothing")
        {
                fake_sdu sdu1(&ctx, 1, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1.get());

                deliver_ready(&ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(events.empty());
        }

        REQUIRE(::rlc_sched_deinit(&ctx.sched) == 0);
}

TEST_CASE("alarm_reassembly", "[rx][static]")
{
        /* Spec 5.2.2.2.4 / 5.2.3.2.4, "when t-Reassembly expires". Shared
         * between UM and AM, so run every case under both. */
        auto type = GENERATE(RLC_AM, RLC_UM);

        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        const ::rlc_config conf = {
                .type = type,
                .window_size = 10,
                .time_reassembly_us = 5000000,
                .sn_width = RLC_SN_12BIT,
        };

        fixture::rlc_ctx fx;
        ::rlc_context &ctx = *fx.get();
        event::event_handler events;
        ctx.conf = &conf;
        ctx.alloc_misc = mem::alloc;
        fx.on_event(events.listener());
        ctx.listener = fixture::rlc_ctx::listener_trampoline;
        ::rlc_list_init(&ctx.rx.sdus);
        ::rlc_window_init(&ctx.rx.win, 0, 10);
        REQUIRE(::rlc_sched_init(&ctx.sched) == 0);
        REQUIRE(::gabs_timer_ctx_init(&ctx.timer_ctx) == 0);
        REQUIRE(::rlc_timer_install(&ctx.rx.t_reassembly, alarm_reassembly,
                                    &ctx) == 0);

        SECTION("nothing left pending: delivers, drops, no restart")
        {
                /* Nothing pending at or after the trigger, so the window
                 * advances to RX_Next_Highest, the completed SDU is
                 * delivered, the incomplete one dropped, no restart. */
                ctx.rx.next_status_trigger = 2;
                ctx.rx.next_highest = 2;

                fake_sdu sdu0(&ctx, 0, RLC_DONE);
                fake_sdu sdu1(&ctx, 1, RLC_READY);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1.get());

                alarm_reassembly(&ctx.rx.t_reassembly, &ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(::rlc_window_base(&ctx.rx.win) == 2);

                REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_DONE).sn == 0);
                REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_FAIL).sn == 1);
                REQUIRE(events.empty());

                REQUIRE(gabs_override::armed(ctx.rx.t_reassembly.gtimer) ==
                       false);
        }

        SECTION("gap remains after expiry: delivers below base, restarts")
        {
                /* The window stops at the first incomplete SDU. DONE SDUs
                 * below the new base are delivered, the rest stay queued,
                 * and the remaining gap restarts the timer. */
                ctx.rx.next_status_trigger = 1;
                ctx.rx.next_highest = 3;

                fake_sdu sdu0(&ctx, 0, RLC_DONE);
                fake_sdu sdu1(&ctx, 1, RLC_READY);
                fake_sdu sdu2(&ctx, 2, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1.get());
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2.get());

                alarm_reassembly(&ctx.rx.t_reassembly, &ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(::rlc_window_base(&ctx.rx.win) == 1);

                REQUIRE(events.pop(::rlc_event::RLC_EVENT_RX_DONE).sn == 0);
                REQUIRE(events.empty());

                REQUIRE(::rlc_sdu_queue_get(&ctx.rx.sdus, 1) == sdu1.get());
                REQUIRE(::rlc_sdu_queue_get(&ctx.rx.sdus, 2) == sdu2.get());

                REQUIRE(ctx.rx.next_status_trigger == 3);
                REQUIRE(gabs_override::armed(ctx.rx.t_reassembly.gtimer) ==
                       true);
        }

        REQUIRE(::rlc_timer_uninstall(&ctx.rx.t_reassembly) == 0);
        REQUIRE(::gabs_timer_ctx_deinit(&ctx.timer_ctx) == 0);
        REQUIRE(::rlc_sched_deinit(&ctx.sched) == 0);
}
}; // namespace rlc::test
