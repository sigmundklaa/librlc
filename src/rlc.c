
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include <rlc/rlc.h>
#include <rlc/timer.h>
#include <rlc/plat.h>
#include <rlc/chunks.h>
#include <rlc/buf.h>

#include "arq.h"
#include "tx.h"
#include "rx.h"
#include "sdu.h"
#include "methods.h"

rlc_errno rlc_init(struct rlc_context *ctx, enum rlc_sdu_type type,
                   const struct rlc_config *config,
                   const struct rlc_methods *methods, void *user_data)
{
        (void)memset(ctx, 0, sizeof(*ctx));

        ctx->type = type;
        ctx->methods = methods;
        ctx->conf = config;
        ctx->user_data = user_data;

        rlc_lock_init(&ctx->lock);

        rlc_window_init(&ctx->tx.win, 0, config->window_size);
        rlc_window_init(&ctx->rx.win, 0, config->window_size);

        rlc_arq_init(ctx);
        rlc_rx_init(ctx);

        return 0;
}

rlc_errno rlc_send(struct rlc_context *ctx, struct rlc_buf *buf)
{
        rlc_errno status;
        struct rlc_segment seg;
        struct rlc_sdu *sdu;

        if (!rlc_window_has(&ctx->tx.win, ctx->tx.next_sn)) {
                return -ENOSPC;
        }

        sdu = rlc_sdu_alloc(ctx, RLC_TX, buf);
        if (sdu == NULL) {
                return -ENOMEM;
        }

        sdu->sn = ctx->tx.next_sn++;

        rlc_lock_acquire(&ctx->lock);

        rlc_sdu_insert(ctx, sdu);

        seg.start = 0;
        seg.end = sdu->buffer->size;

        rlc_dbgf("TX; Queueing SDU %" PRIu32 ", RANGE: %" PRIu32 "->%" PRIu32,
                 sdu->sn, seg.start, seg.end);

        status = rlc_sdu_seg_append(ctx, sdu, seg);
        if (status != 0) {
                return status;
        }

        rlc_lock_release(&ctx->lock);

        return rlc_tx_request(ctx);
}

void rlc_tx_avail(struct rlc_context *ctx, size_t size)
{
        rlc_lock_acquire(&ctx->lock);

        rlc_dbgf("TX availability for context %p", ctx);

        size -= rlc_arq_tx_yield(ctx, size);
        if (size > 0) {
                size -= rlc_tx_yield(ctx, size);
        }

        rlc_lock_release(&ctx->lock);
}
