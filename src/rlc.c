
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <errno.h>

#include <rlc/rlc.h>
#include <rlc/timer.h>
#include <rlc/plat.h>
#include <rlc/sdu.h>

#include "arq.h"
#include "log.h"

rlc_errno rlc_init(struct rlc_context *ctx, const struct rlc_config *config,
                   const struct rlc_methods *methods,
                   const gabs_allocator_h *misc_allocator,
                   const gabs_allocator_h *buf_allocator)
{
        rlc_errno status;
        (void)memset(ctx, 0, sizeof(*ctx));

        status = rlc_plat_init(&ctx->platform);
        if (status != 0) {
                return status;
        }

        /* TODO: logger */

        ctx->methods = methods;
        ctx->conf = config;

        ctx->alloc_misc = misc_allocator;
        ctx->alloc_buf = buf_allocator;

        status = gabs_mutex_init(&ctx->lock);
        if (status != 0) {
                return status;
        }

        rlc_window_init(&ctx->tx.win, 0, config->window_size);
        rlc_window_init(&ctx->rx.win, 0, config->window_size);

        status = rlc_arq_init(ctx);
        if (status != 0) {
                (void)gabs_mutex_deinit(&ctx->lock);
                return status;
        }

        status = rlc_rx_init(ctx);
        if (status != 0) {
                (void)rlc_arq_deinit(ctx);
                (void)gabs_mutex_deinit(&ctx->lock);

                return status;
        }

        return 0;
}

rlc_errno rlc_deinit(struct rlc_context *ctx)
{
        rlc_errno status;
        struct rlc_sdu *sdu;

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                rlc_sdu_decref(ctx, sdu);
        }

        status = rlc_rx_deinit(ctx);
        if (status != 0) {
                return status;
        }

        status = rlc_arq_deinit(ctx);
        if (status != 0) {
                return status;
        }

        status = gabs_mutex_deinit(&ctx->lock);
        if (status != 0) {
                return status;
        }

        status = rlc_plat_deinit(&ctx->platform);

        return status;
}

rlc_errno rlc_reset(struct rlc_context *ctx)
{
        rlc_errno status;
        struct rlc_sdu *sdu;

        status = rlc_plat_reset(&ctx->platform);
        if (status != 0) {
                return status;
        }

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                rlc_sdu_decref(ctx, sdu);
        }

        ctx->rx.next_highest = 0;
        ctx->rx.highest_ack = 0;
        ctx->rx.next_status_trigger = 0;
        ctx->tx.next_sn = 0;
        ctx->tx.retx_count = 0;
        ctx->tx.pdu_without_poll = 0;
        ctx->tx.byte_without_poll = 0;
        ctx->poll_sn = 0;
        ctx->force_poll = 0;
        ctx->gen_status = 0;

        rlc_window_init(&ctx->tx.win, 0, ctx->conf->window_size);
        rlc_window_init(&ctx->rx.win, 0, ctx->conf->window_size);

        return 0;
}
