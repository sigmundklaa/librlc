
#include <rlc/timer.h>
#include <rlc/rlc.h>

#include "common.h"

void rlc_timer_alarm(rlc_timer timer, struct rlc_context *ctx, rlc_timer_cb cb)
{
        rlc_lock_acquire(&ctx->lock);

        /* Ensure timer has not been stopped while in the process of firing.
         * This is assuming that the stopping function holds the lock of the
         * context. */
        if (!rlc_timer_active(timer)) {
                rlc_lock_release(&ctx->lock);
                return;
        }

        if (rlc_timer_flags(timer) & RLC_TIMER_UNLOCKED_CB) {
                rlc_lock_release(&ctx->lock);
                cb(timer, ctx);

        } else {
                cb(timer, ctx);
                rlc_lock_release(&ctx->lock);
        }
}
