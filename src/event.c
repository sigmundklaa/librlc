
#include <rlc/chunks.h>

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

        rlc_inff("RX; SDU %" PRIu32 " received (%" PRIu32 "B)", sdu->sn,
                 sdu->segments->seg.end);

        event.type = RLC_EVENT_RX_DONE;
        event.data.rx_done.data = sdu->rx_buffer;
        event.data.rx_done.size = sdu->segments->seg.end;

        rlc_event_fire(ctx, &event);
}

void rlc_event_rx_done_direct(struct rlc_context *ctx,
                              const struct rlc_chunk *chunks)
{
        struct rlc_event event;

        rlc_inff("RX; Full SDU delivered (%zuB)", rlc_chunks_size(chunks));

        event.type = RLC_EVENT_RX_DONE;
        event.data.rx_done = *chunks;

        rlc_event_fire(ctx, &event);
}

void rlc_event_tx_done(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        rlc_inff("TX; SDU %" PRIu32 " transmitted (%zuB)", sdu->sn,
                 rlc_chunks_size(sdu->chunks));

        event.type = RLC_EVENT_TX_DONE;
        event.data.tx_done = sdu->chunks;

        rlc_event_fire(ctx, &event);
}

void rlc_event_rx_drop(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        rlc_wrnf("Dropping SN=%" PRIu32, sdu->sn);

        event.type = RLC_EVENT_RX_FAIL;
        event.data.rx_fail.sn = sdu->sn;

        rlc_event_fire(ctx, &event);
}
