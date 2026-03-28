
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <errno.h>

#include <rlc/rlc.h>
#include <rlc/timer.h>
#include <rlc/sdu.h>

#include "arq.h"
#include "common.h"
#include "log.h"

static const struct rlc_config default_config = {
        .type = RLC_AM,
        .window_size = 10,
        .pdu_without_poll_max = 3,
        .byte_without_poll_max = 1500,
        .time_reassembly_us = 500e3,
        .time_poll_retransmit_us = 50e3,
        .time_status_prohibit_us = 5e3,
        .max_retx_threshhold = 3,
        .sn_width = RLC_SN_18BIT,
};

rlc_errno rlc_init(struct rlc_context *ctx, const struct rlc_backend *backend,
                   const gabs_allocator_h *misc_allocator,
                   const gabs_allocator_h *buf_allocator)
{
        rlc_errno status;
        (void)memset(ctx, 0, sizeof(*ctx));

        ctx->conf = &default_config;
        ctx->backend = backend;

        ctx->alloc_misc = misc_allocator;
        ctx->alloc_buf = buf_allocator;

        status = gabs_mutex_init(&ctx->lock);
        if (status != 0) {
                return status;
        }

        status = gabs_timer_ctx_init(&ctx->timer_ctx);
        if (status != 0) {
                return status;
        }

        status = rlc_sched_init(&ctx->sched);
        if (status != 0) {
                (void)gabs_mutex_deinit(&ctx->lock);
                return status;
        }

        rlc_tx_init(ctx);

        status = rlc_arq_init(ctx);
        if (status != 0) {
                rlc_tx_deinit(ctx);
                (void)rlc_sched_deinit(&ctx->sched);
                (void)gabs_mutex_deinit(&ctx->lock);
                return status;
        }

        status = rlc_rx_init(ctx);
        if (status != 0) {
                (void)rlc_arq_deinit(ctx);
                rlc_tx_deinit(ctx);
                (void)rlc_sched_deinit(&ctx->sched);
                (void)gabs_mutex_deinit(&ctx->lock);

                return status;
        }

        return 0;
}

rlc_errno rlc_attach_listener(struct rlc_context *ctx,
                              rlc_event_listener listener)
{
        rlc_errno status;

        rlc_lock_acquire(&ctx->lock);

        status = 0;

        if (ctx->listener != NULL) {
                status = -EBUSY;
        } else {
                ctx->listener = listener;
        }

        rlc_lock_release(&ctx->lock);

        return status;
}

void rlc_detach_listener(struct rlc_context *ctx)
{
        rlc_lock_acquire(&ctx->lock);
        ctx->listener = NULL;
        rlc_lock_release(&ctx->lock);
}

rlc_errno rlc_deinit(struct rlc_context *ctx)
{
        rlc_errno status;

        rlc_tx_deinit(ctx);

        status = rlc_rx_deinit(ctx);
        if (status != 0) {
                return status;
        }

        status = rlc_arq_deinit(ctx);
        if (status != 0) {
                return status;
        }

        status = rlc_sched_deinit(&ctx->sched);
        if (status != 0) {
                return status;
        }

        status = gabs_timer_ctx_deinit(&ctx->timer_ctx);
        if (status != 0) {
                return status;
        }

        status = gabs_mutex_deinit(&ctx->lock);
        if (status != 0) {
                return status;
        }

        return status;
}

rlc_errno rlc_reset(struct rlc_context *ctx)
{
        rlc_lock_acquire(&ctx->lock);

        rlc_sched_reset(&ctx->sched);
        rlc_arq_reset(ctx);
        rlc_tx_reset(ctx);
        rlc_rx_reset(ctx);

        rlc_lock_release(&ctx->lock);

        return 0;
}
