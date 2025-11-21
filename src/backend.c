
#include <rlc/sdu.h>

#include "backend.h"
#include "encode.h"
#include "methods.h"
#include "utils.h"
#include "log.h"

ptrdiff_t rlc_backend_tx_submit(struct rlc_context *ctx, struct rlc_pdu *pdu,
                                gnb_h buf)
{
        ptrdiff_t status;
        ptrdiff_t size;
        gnb_h header;

        header = gnb_new(&ctx->alloc_gnb, RLC_PDU_HEADER_MAX_SIZE);
        if (!gnb_okay(header)) {
                rlc_panicf(ENOMEM, "Buffer alloc");
                return -ENOMEM;
        }

        rlc_pdu_encode(ctx, pdu, &header);

        gnb_chain_front(&buf, header);
        size = gnb_size(buf);

        /* Submit is given ownership of this buffer - it is free to modify it
         * as it pleases. */
        status = rlc_tx_submit(ctx, buf);

        if (status != 0) {
                return status;
        }

        return size;
}

static void do_tx_request(rlc_timer timer, struct rlc_context *ctx)
{
        (void)timer;
        (void)rlc_tx_request(ctx);
}

void rlc_backend_tx_request(struct rlc_context *ctx, bool offload)
{
        rlc_timer timer;
        rlc_errno status;

        if (!offload) {
                (void)rlc_tx_request(ctx);
                return;
        }

        timer = rlc_timer_install(do_tx_request, ctx,
                                  RLC_TIMER_SINGLE | RLC_TIMER_UNLOCKED_CB);
        if (!rlc_timer_okay(timer)) {
                rlc_panicf(EBUSY, "Failed to alloc timer");
                return;
        }

        status = rlc_timer_start(timer, 1);
        if (status != 0) {
                rlc_panicf(status, "Unable to start timer");
        }
}
