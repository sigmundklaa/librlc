
#include <rlc/rlc.h>

#include "log.h"

static struct rlc_event *event_get(struct rlc_sched_item *item)
{
        return gabs_container_of(item, struct rlc_event, sched);
}

static void event_fire(struct rlc_context *ctx, struct rlc_event *event)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->event == NULL) {
                rlc_assert(0);
                return;
        }

        methods->event(ctx, event);
}

static void event_dealloc(struct rlc_sched_item *item)
{
        struct rlc_event *event;
        int status;

        event = event_get(item);

        switch (event->type) {
        case RLC_EVENT_RX_DONE_DIRECT:
                gabs_pbuf_decref(*event->buf);
        default:
                rlc_sdu_decref(event->sdu);
        }

        status = gabs_dealloc(event->ctx->alloc_misc, event);
        if (status != 0) {
                gabs_log_errf(event->ctx->logger, "Failed to dealloc event: %i",
                              status);
        }
}

static void event_sched_cb(struct rlc_sched_item *item)
{
        struct rlc_event *event;

        event = event_get(item);
        event_fire(event->ctx, event);

        event_dealloc(item);
}

static struct rlc_event *event_alloc(struct rlc_context *ctx)
{
        int status;
        struct rlc_event *mem;

        status = gabs_alloc(ctx->alloc_misc, sizeof(struct rlc_event),
                            (void **)&mem);
        if (status != 0) {
                gabs_log_errf(ctx->logger, "Failed to allocate event: %i",
                              status);
                return NULL;
        }

        mem->ctx = ctx;

        rlc_sched_item_init(&mem->sched, event_sched_cb, event_dealloc);

        return mem;
}

static void sdu_event(struct rlc_context *ctx, struct rlc_sdu *sdu,
                      enum rlc_event_type type)
{
        struct rlc_event *event;

        event = event_alloc(ctx);
        if (event == NULL) {
                return;
        }

        event->type = type;
        event->sdu = sdu;

        rlc_sdu_incref(sdu);
        rlc_sched_put(&ctx->sched, &event->sched);
}

void rlc_event_rx_done(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        gabs_log_inff(ctx->logger,
                      "RX; SDU %" PRIu32 " received (%" PRIu32 "B)", sdu->sn,
                      sdu->segments->seg.end);

        sdu_event(ctx, sdu, RLC_EVENT_RX_DONE);
}

void rlc_event_rx_done_direct(struct rlc_context *ctx, gabs_pbuf *buf)
{
        struct rlc_event *event;

        gabs_log_inff(ctx->logger, "RX; Full SDU delivered (%zuB)",
                      gabs_pbuf_size(*buf));

        event = event_alloc(ctx);
        if (event == NULL) {
                return;
        }

        event->type = RLC_EVENT_RX_DONE_DIRECT;
        event->buf = buf;

        gabs_pbuf_incref(*buf);
        rlc_sched_put(&ctx->sched, &event->sched);
}

void rlc_event_tx_done(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        gabs_log_inff(ctx->logger, "TX; SDU %" PRIu32 " transmitted (%zuB)",
                      sdu->sn, gabs_pbuf_size(sdu->buffer));

        sdu_event(ctx, sdu, RLC_EVENT_TX_DONE);
}

void rlc_event_rx_drop(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        gabs_log_wrnf(ctx->logger, "Dropping SN=%" PRIu32, sdu->sn);

        sdu_event(ctx, sdu, RLC_EVENT_RX_FAIL);
}

void rlc_event_tx_fail(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        gabs_log_errf(ctx->logger, "Failed transmit of SN=%" PRIu32, sdu->sn);

        sdu_event(ctx, sdu, RLC_EVENT_TX_FAIL);
}
