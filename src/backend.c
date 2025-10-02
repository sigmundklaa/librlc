
#include <rlc/buf.h>
#include <rlc/sdu.h>

#include "backend.h"
#include "encode.h"

ssize_t rlc_backend_tx_submit(struct rlc_context *ctx, struct rlc_pdu *pdu,
                              rlc_buf *buf)
{
        ssize_t status;
        ssize_t size;

        rlc_pdu_encode(ctx, pdu, buf);
        size = rlc_buf_size(buf);

        status = rlc_tx_submit(ctx, buf);
        if (status != 0) {
                return status;
        }

        return size;
}
