
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <errno.h>

#include <rlc/rlc.h>
#include <rlc/timer.h>
#include <rlc/plat.h>
#include <rlc/buf.h>
#include <rlc/sdu.h>

#include "arq.h"
#include "rx.h"
#include "log.h"
#include "backend.h"

rlc_errno rlc_init(struct rlc_context *ctx, enum rlc_sdu_type type,
                   const struct rlc_config *config,
                   const struct rlc_methods *methods, void *user_data)
{
        rlc_errno status;
        (void)memset(ctx, 0, sizeof(*ctx));

        ctx->type = type;
        ctx->methods = methods;
        ctx->conf = config;
        ctx->user_data = user_data;

        rlc_lock_init(&ctx->lock);

        rlc_window_init(&ctx->tx.win, 0, config->window_size);
        rlc_window_init(&ctx->rx.win, 0, config->window_size);

        status = rlc_arq_init(ctx);
        if (status != 0) {
                rlc_lock_deinit(&ctx->lock);
                return status;
        }

        status = rlc_rx_init(ctx);
        if (status != 0) {
                (void)rlc_arq_deinit(ctx);
                rlc_lock_deinit(&ctx->lock);

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

        rlc_lock_deinit(&ctx->lock);

        return 0;
}

rlc_errno rlc_reset(struct rlc_context *ctx)
{
        rlc_errno status;

        status = rlc_deinit(ctx);
        if (status != 0) {
                return status;
        }

        return rlc_init(ctx, ctx->type, ctx->conf, ctx->methods,
                        ctx->user_data);
}

rlc_errno rlc_send(struct rlc_context *ctx, rlc_buf *buf,
                   struct rlc_sdu **sdu_out)
{
        struct rlc_segment seg;
        struct rlc_sdu *sdu;
        rlc_errno status;

        if (!rlc_window_has(&ctx->tx.win, ctx->tx.next_sn)) {
                return -ENOSPC;
        }

        sdu = rlc_sdu_alloc(ctx, RLC_TX);
        if (sdu == NULL) {
                return -ENOMEM;
        }

        rlc_buf_incref(buf);

        sdu->sn = ctx->tx.next_sn++;
        sdu->buffer = buf;

        rlc_lock_acquire(&ctx->lock);

        seg.start = 0;
        seg.end = rlc_buf_size(sdu->buffer);

        rlc_dbgf("TX; Queueing SDU %" PRIu32 ", RANGE: %" PRIu32 "->%" PRIu32,
                 sdu->sn, seg.start, seg.end);

        status = rlc_sdu_seg_insert_all(ctx, sdu, seg);
        if (status != 0) {
                rlc_lock_release(&ctx->lock);
                rlc_sdu_decref(ctx, sdu);
                rlc_buf_decref(buf, ctx);

                return status;
        }

        rlc_sdu_insert(ctx, sdu);

        if (sdu_out != NULL) {
                *sdu_out = sdu;

                rlc_sdu_incref(sdu);
        }

        rlc_lock_release(&ctx->lock);

        rlc_backend_tx_request(ctx, false);
        return 0;
}
