
#include <rlc/timer.h>
#include <rlc/rlc.h>

#include "common.h"

static void timer_alarm(gabs_timer gtimer, void *user_data)
{
        struct rlc_timer *timer = user_data;
        struct rlc_context *ctx = timer->ctx;

        rlc_lock_acquire(&ctx->lock);

        /* Ensure timer has not been stopped while in the process of firing.
         * This is assuming that the stopping function holds the lock of the
         * context. */
        if (!gabs_timer_active(gtimer)) {
                rlc_lock_release(&ctx->lock);
                return;
        }

        rlc_assert(timer->cb != NULL);
        timer->cb(timer, ctx);

        rlc_lock_release(&ctx->lock);
        rlc_sched_yield(&ctx->sched);
}

rlc_errno rlc_timer_install(struct rlc_timer *timer, rlc_timer_cb cb,
                            struct rlc_context *ctx)
{
        timer->cb = cb;
        timer->ctx = ctx;
        timer->gtimer = gabs_timer_install(&ctx->timer_ctx, timer_alarm, timer);

        if (!gabs_timer_okay(timer->gtimer)) {
                return -ENODEV;
        }

        return 0;
}
