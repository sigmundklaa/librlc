
#include <rlc/buf.h>
#include <rlc/sdu.h>

#include "backend.h"
#include "encode.h"

ssize_t rlc_backend_tx_submit(struct rlc_context *ctx, struct rlc_pdu *pdu,
                              rlc_buf *buf)
{
        ssize_t status;
        ssize_t size;
        rlc_buf *header;

        header = rlc_buf_alloc(ctx, RLC_PDU_HEADER_MAX_SIZE);
        if (header == NULL) {
                rlc_panicf(ENOMEM, "Buffer alloc");
                return -ENOMEM;
        }

        rlc_pdu_encode(ctx, pdu, header);

        rlc_buf_incref(buf);

        buf = rlc_buf_chain_front(buf, header);
        size = rlc_buf_size(buf);

        status = rlc_tx_submit(ctx, buf);
        rlc_buf_decref(buf, ctx);

        if (status != 0) {
                return status;
        }

        return size;
}
