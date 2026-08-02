
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>
#include <rlc/seg_buf.h>

#include "util/mem.hh"
#include "util/buf.hh"

#include "gabs-overrides/timer/timer.hh"

extern "C" {
#include "../src/rx.c"
}

namespace rlc::test
{

using namespace util;

namespace
{

::rlc_sdu *make_sdu(::rlc_context *ctx, std::uint32_t sn,
                    enum rlc_sdu_state state)
{
        auto sdu = ::rlc_sdu_alloc(ctx, false);
        REQUIRE(sdu != nullptr);

        sdu->sn = sn;
        sdu->state = state;

        return sdu;
}

struct captured_event {
        int type;
        std::uint32_t sn;
};

/*
 * ctx->listener is a plain C function pointer with no user-data slot, so a
 * capturing closure can't be assigned to it directly. `ctx` is embedded as a
 * member of this fixture instead, and gabs_container_of recovers the
 * fixture (and its local `events` vector) from the ctx pointer the listener
 * is called with - the same pattern util::backend uses for rlc_backend.
 */
struct rx_fixture {
        ::rlc_context ctx{};
        std::vector<captured_event> events;
};

constexpr auto capture_listener = [](::rlc_context *raw_ctx,
                                     const ::rlc_event *ev) {
        auto *fx = gabs_container_of(raw_ctx, rx_fixture, ctx);

        fx->events.push_back({static_cast<int>(ev->type),
                              ev->sdu != nullptr ? ev->sdu->sn : 0});
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

                auto sdu = make_sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{0, 3}, mem::alloc,
                                             mem::alloc) == 0);
                REQUIRE(::rlc_seg_buf_insert(&sdu->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{5, 8}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu);

                REQUIRE(should_start_reassembly(&ctx) == true);

                ::rlc_sdu_decref(sdu);
        }

        SECTION("one pending SDU fully contiguous does not trigger start")
        {
                ctx.rx.next_highest = 1;

                auto sdu = make_sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{0, 5}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu);

                REQUIRE(should_start_reassembly(&ctx) == false);

                ::rlc_sdu_decref(sdu);
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

                auto sdu = make_sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{0, 5}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu);

                REQUIRE(should_stop_reassembly(&ctx) == true);

                ::rlc_sdu_decref(sdu);
        }

        SECTION("trigger one past base, SDU still gapped, does not stop")
        {
                ctx.rx.next_status_trigger = 1;

                auto sdu = make_sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{0, 3}, mem::alloc,
                                             mem::alloc) == 0);
                REQUIRE(::rlc_seg_buf_insert(&sdu->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{5, 8}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu);

                REQUIRE(should_stop_reassembly(&ctx) == false);

                ::rlc_sdu_decref(sdu);
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

                auto sdu = make_sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{0, 3}, mem::alloc,
                                             mem::alloc) == 0);
                REQUIRE(::rlc_seg_buf_insert(&sdu->rx.buffer,
                                             buf::create(std::string(3, 'x')),
                                             ::rlc_seg{5, 8}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu);

                REQUIRE(should_restart_reassembly(&ctx) == true);

                ::rlc_sdu_decref(sdu);
        }

        SECTION("one pending SDU fully contiguous does not restart")
        {
                ctx.rx.next_highest = 1;

                auto sdu = make_sdu(&ctx, 0, RLC_READY);
                REQUIRE(::rlc_seg_buf_insert(&sdu->rx.buffer,
                                             buf::create(std::string(5, 'x')),
                                             ::rlc_seg{0, 5}, mem::alloc,
                                             mem::alloc) == 0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu);

                REQUIRE(should_restart_reassembly(&ctx) == false);

                ::rlc_sdu_decref(sdu);
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
                auto sdu0 = make_sdu(&ctx, 0, RLC_DONE);
                auto sdu1 = make_sdu(&ctx, 1, RLC_DONE);
                auto sdu2 = make_sdu(&ctx, 2, RLC_READY);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2);

                REQUIRE(lowest_sn_not_recv(&ctx) == 2);

                ::rlc_sdu_decref(sdu0);
                ::rlc_sdu_decref(sdu1);
                ::rlc_sdu_decref(sdu2);
        }

        SECTION("returns the missing SN at a gap in the sequence")
        {
                auto sdu0 = make_sdu(&ctx, 0, RLC_DONE);
                auto sdu2 = make_sdu(&ctx, 2, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2);

                REQUIRE(lowest_sn_not_recv(&ctx) == 1);

                ::rlc_sdu_decref(sdu0);
                ::rlc_sdu_decref(sdu2);
        }

        SECTION("all contiguous and done returns RX_Next_Highest")
        {
                auto sdu0 = make_sdu(&ctx, 0, RLC_DONE);
                auto sdu1 = make_sdu(&ctx, 1, RLC_DONE);
                auto sdu2 = make_sdu(&ctx, 2, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2);

                ctx.rx.next_highest = 3;

                REQUIRE(lowest_sn_not_recv(&ctx) == 3);

                ::rlc_sdu_decref(sdu0);
                ::rlc_sdu_decref(sdu1);
                ::rlc_sdu_decref(sdu2);
        }
}

TEST_CASE("deliver_ready", "[rx][static]")
{
        rx_fixture fx;
        ::rlc_context &ctx = fx.ctx;

        ::rlc_list_init(&ctx.rx.sdus);
        ::rlc_window_init(&ctx.rx.win, 0, 10);
        ctx.alloc_misc = mem::alloc;
        ctx.listener = capture_listener;
        REQUIRE(::rlc_sched_init(&ctx.sched) == 0);

        SECTION("delivers contiguous DONE prefix in order")
        {
                auto sdu0 = make_sdu(&ctx, 0, RLC_DONE);
                auto sdu1 = make_sdu(&ctx, 1, RLC_DONE);
                auto sdu2 = make_sdu(&ctx, 2, RLC_READY);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2);

                deliver_ready(&ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(fx.events.size() == 2);
                REQUIRE(fx.events[0].sn == 0);
                REQUIRE(fx.events[1].sn == 1);

                /* sdu2 is not DONE, so it remains queued, undelivered. */
                REQUIRE(::rlc_sdu_queue_get(&ctx.rx.sdus, 2) == sdu2);

                ::rlc_sdu_decref(sdu2);
        }

        SECTION("stops at first gap even if a later SDU is DONE")
        {
                auto sdu0 = make_sdu(&ctx, 0, RLC_DONE);
                auto sdu2 = make_sdu(&ctx, 2, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2);

                deliver_ready(&ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(fx.events.size() == 1);
                REQUIRE(fx.events[0].sn == 0);

                REQUIRE(::rlc_sdu_queue_get(&ctx.rx.sdus, 2) == sdu2);

                ::rlc_sdu_decref(sdu2);
        }

        SECTION("nothing ready at the base delivers nothing")
        {
                auto sdu1 = make_sdu(&ctx, 1, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1);

                deliver_ready(&ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(fx.events.empty());

                ::rlc_sdu_decref(sdu1);
        }

        REQUIRE(::rlc_sched_deinit(&ctx.sched) == 0);
}

TEST_CASE("alarm_reassembly", "[rx][static]")
{
        /* Spec 5.2.2.2.4 / 5.2.3.2.4, "when t-Reassembly expires". */
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);

        static const ::rlc_config conf = {
                .type = RLC_UM,
                .window_size = 10,
                .time_reassembly_us = 5000000,
                .sn_width = RLC_SN_12BIT,
        };

        rx_fixture fx;
        ::rlc_context &ctx = fx.ctx;
        ctx.conf = &conf;
        ctx.alloc_misc = mem::alloc;
        ctx.listener = capture_listener;
        ::rlc_list_init(&ctx.rx.sdus);
        ::rlc_window_init(&ctx.rx.win, 0, 10);
        REQUIRE(::rlc_sched_init(&ctx.sched) == 0);
        REQUIRE(::gabs_timer_ctx_init(&ctx.timer_ctx) == 0);
        REQUIRE(::rlc_timer_install(&ctx.rx.t_reassembly, alarm_reassembly,
                                    &ctx) == 0);

        SECTION("nothing left pending: delivers, drops, no restart")
        {
                /* Window advances all the way to RX_Next_Highest since
                 * nothing remains pending at or after the trigger; the
                 * completed SDU is delivered and the incomplete one below
                 * the new base is dropped, and the timer is not
                 * restarted. */
                ctx.rx.next_status_trigger = 2;
                ctx.rx.next_highest = 2;

                auto sdu0 = make_sdu(&ctx, 0, RLC_DONE);
                auto sdu1 = make_sdu(&ctx, 1, RLC_READY);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1);

                alarm_reassembly(&ctx.rx.t_reassembly, &ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(::rlc_window_base(&ctx.rx.win) == 2);

                REQUIRE(fx.events.size() == 2);
                REQUIRE(fx.events[0].sn == 0);
                REQUIRE(fx.events[1].sn == 1);

                REQUIRE(::rlc_timer_active(&ctx.rx.t_reassembly) == false);
        }

        SECTION("gap remains after expiry: delivers below base, restarts")
        {
                /* Window advances only to the first still-incomplete SDU at
                 * or after the trigger; SDUs below the new base that are
                 * DONE are delivered, and the rest stay queued. Since a gap
                 * remains, the timer is restarted. */
                ctx.rx.next_status_trigger = 1;
                ctx.rx.next_highest = 3;

                auto sdu0 = make_sdu(&ctx, 0, RLC_DONE);
                auto sdu1 = make_sdu(&ctx, 1, RLC_READY);
                auto sdu2 = make_sdu(&ctx, 2, RLC_DONE);

                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu0);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu1);
                ::rlc_sdu_queue_insert(&ctx.rx.sdus, sdu2);

                alarm_reassembly(&ctx.rx.t_reassembly, &ctx);
                ::rlc_sched_yield(&ctx.sched);

                REQUIRE(::rlc_window_base(&ctx.rx.win) == 1);

                REQUIRE(fx.events.size() == 1);
                REQUIRE(fx.events[0].sn == 0);

                REQUIRE(::rlc_sdu_queue_get(&ctx.rx.sdus, 1) == sdu1);
                REQUIRE(::rlc_sdu_queue_get(&ctx.rx.sdus, 2) == sdu2);

                REQUIRE(ctx.rx.next_status_trigger == 3);
                REQUIRE(::rlc_timer_active(&ctx.rx.t_reassembly) == true);

                ::rlc_sdu_decref(sdu1);
                ::rlc_sdu_decref(sdu2);
        }

        REQUIRE(::rlc_timer_uninstall(&ctx.rx.t_reassembly) == 0);
        REQUIRE(::gabs_timer_ctx_deinit(&ctx.timer_ctx) == 0);
        REQUIRE(::rlc_sched_deinit(&ctx.sched) == 0);
}

}; // namespace rlc::test
