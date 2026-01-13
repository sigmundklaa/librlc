
#include <rlc/sdu.h>
#include <rlc/utils.h>
#include <rlc/sched.h>

#include "backend.h"
#include "encode.h"
#include "methods.h"

typedef void (*offload_fn)(struct rlc_sched_item *);

union offload_arg {
        gabs_pbuf buf;
        char unused;
};

struct offload_item {
        offload_fn fn;
        union offload_arg arg;
        struct rlc_sched_item sched_item;
        struct rlc_context *ctx;
};

static struct offload_item *offload_get(struct rlc_sched_item *item)
{
        return gabs_container_of(item, struct offload_item, sched_item);
}

static void offload_dealloc(struct rlc_sched_item *item)
{
        int status;
        struct offload_item *offload;
        const gabs_logger_h *logger;

        offload = offload_get(item);
        logger = offload->ctx->logger;

        status = gabs_dealloc(offload->ctx->alloc_misc, offload);
        if (status != 0) {
                gabs_log_errf(logger, "Unable to dealloc offload: %i", status);
        }
}

static void offload_tx_submit(struct rlc_sched_item *item)
{
        rlc_errno status;
        struct offload_item *offload;

        offload = offload_get(item);
        status = rlc_tx_submit(offload->ctx, offload->arg.buf);
        if (status != 0) {
                gabs_log_errf(offload->ctx->logger, "Unable to TX: %i", status);
        }

        offload_dealloc(item);
}

static void offload_tx_request(struct rlc_sched_item *item)
{
        rlc_errno status;
        struct offload_item *offload;

        offload = offload_get(item);
        status = rlc_tx_request(offload->ctx);
        if (status != 0) {
                gabs_log_errf(offload->ctx->logger, "Unable to request TX: %i",
                              status);
        }

        offload_dealloc(item);
}

static void offload_call(struct rlc_context *ctx, offload_fn fn,
                         union offload_arg arg)
{
        int status;
        struct offload_item *offload;

        status = gabs_alloc(ctx->alloc_misc, sizeof(*offload),
                            (void **)&offload);
        if (status != 0) {
                gabs_log_errf(offload->ctx->logger,
                              "Unable to allocate offload request: %i", status);
                return;
        }

        offload->ctx = ctx;
        offload->arg = arg;
        rlc_sched_item_init(&offload->sched_item, fn, offload_dealloc);

        rlc_sched_put(&ctx->sched, &offload->sched_item);
}

ptrdiff_t rlc_backend_tx_submit(struct rlc_context *ctx, struct rlc_pdu *pdu,
                                gabs_pbuf buf)
{
        ptrdiff_t size;
        gabs_pbuf header;

        header = gabs_pbuf_new(ctx->alloc_buf, RLC_PDU_HEADER_MAX_SIZE);
        if (!gabs_pbuf_okay(header)) {
                rlc_panicf(ENOMEM, "Buffer alloc");
                return -ENOMEM;
        }

        rlc_pdu_encode(ctx, pdu, &header);

        gabs_pbuf_chain_front(&buf, header);
        size = gabs_pbuf_size(buf);

        offload_call(ctx, offload_tx_submit, (union offload_arg){.buf = buf});

        return size;
}

void rlc_backend_tx_request(struct rlc_context *ctx)
{
        offload_call(ctx, offload_tx_request, (union offload_arg){0});
}
