
#include <errno.h>

#include <rlc/chunks.h>
#include <rlc/buf.h>

#include "event.h"

void rlc_event_fire(struct rlc_context *ctx, struct rlc_event *event)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->event == NULL) {
                rlc_assert(0);
                return;
        }

        methods->event(ctx, event);
}

void rlc_event_rx_done(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;
        struct rlc_chunk chunk;

        rlc_inff("RX; SDU %" PRIu32 " received (%" PRIu32 "B)", sdu->sn,
                 sdu->segments->seg.end);

        event.type = RLC_EVENT_RX_DONE;
        event.data.rx_done = sdu->buffer;

        rlc_event_fire(ctx, &event);
}

void rlc_event_rx_done_direct(struct rlc_context *ctx,
                              const struct rlc_chunk *chunks)
{
        struct rlc_event event;
        struct rlc_buf *buf;
        size_t size;
        ssize_t status;

        size = rlc_chunks_size(chunks);

        rlc_inff("RX; Full SDU delivered (%zuB)", size);

        buf = rlc_buf_alloc(ctx, size);
        if (buf == NULL) {
                rlc_panicf(ENOMEM, "Unable to allocate buffer");
        }

        status = rlc_chunks_deepcopy(chunks, buf->mem, buf->size);
        if (status != (ssize_t)size) {
                rlc_panicf((rlc_errno)status, "Failed to copy to buffer");
        }

        event.type = RLC_EVENT_RX_DONE;
        event.data.rx_done = buf;

        rlc_event_fire(ctx, &event);
        rlc_buf_decref(buf, ctx);
}

void rlc_event_tx_done(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        rlc_inff("TX; SDU %" PRIu32 " transmitted (%zuB)", sdu->sn,
                 sdu->buffer->size);

        event.type = RLC_EVENT_TX_DONE;
        event.data.tx_done = sdu->buffer;

        rlc_event_fire(ctx, &event);
}

void rlc_event_rx_drop(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        rlc_wrnf("Dropping SN=%" PRIu32, sdu->sn);

        event.type = RLC_EVENT_RX_FAIL;

        rlc_event_fire(ctx, &event);
}
