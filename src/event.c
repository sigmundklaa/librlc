
#include <rlc/rlc.h>

#include "event.h"
#include "log.h"

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

        gabs_log_inff(ctx->logger,
                      "RX; SDU %" PRIu32 " received (%" PRIu32 "B)", sdu->sn,
                      sdu->segments->seg.end);

        event.type = RLC_EVENT_RX_DONE;
        event.sdu = sdu;

        rlc_event_fire(ctx, &event);
}

void rlc_event_rx_done_direct(struct rlc_context *ctx, gabs_pbuf *buf)
{
        struct rlc_event event;

        gabs_log_inff(ctx->logger, "RX; Full SDU delivered (%zuB)",
                      gabs_pbuf_size(*buf));

        event.type = RLC_EVENT_RX_DONE_DIRECT;
        event.buf = buf;

        rlc_event_fire(ctx, &event);
}

void rlc_event_tx_done(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        gabs_log_inff(ctx->logger, "TX; SDU %" PRIu32 " transmitted (%zuB)",
                      sdu->sn, gabs_pbuf_size(sdu->buffer));

        event.type = RLC_EVENT_TX_DONE;
        event.sdu = sdu;

        rlc_event_fire(ctx, &event);
}

void rlc_event_rx_drop(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        gabs_log_wrnf(ctx->logger, "Dropping SN=%" PRIu32, sdu->sn);

        event.type = RLC_EVENT_RX_FAIL;
        event.sdu = sdu;

        rlc_event_fire(ctx, &event);
}

void rlc_event_tx_fail(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        gabs_log_errf(ctx->logger, "Failed transmit of SN=%" PRIu32, sdu->sn);

        event.type = RLC_EVENT_TX_FAIL;
        event.sdu = sdu;

        rlc_event_fire(ctx, &event);
}
