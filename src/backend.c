
#include <string.h>

#include <rlc/chunks.h>
#include <rlc/buf.h>

#include "sdu.h"
#include "backend.h"
#include "encode.h"

ssize_t rlc_backend_tx_submit(struct rlc_context *ctx, struct rlc_pdu *pdu,
                              struct rlc_chunk *payload, size_t max_size)
{
        ssize_t status;
        ssize_t total_size;
        uint8_t header[rlc_pdu_header_size(ctx, pdu)];
        struct rlc_chunk chunk;

        /* Size is set by encode */
        chunk.data = header;
        chunk.next = payload;

        (void)memset(header, 0, sizeof(header));

        rlc_pdu_encode(ctx, pdu, &chunk);
        total_size = rlc_chunks_size(&chunk);

        if (total_size > max_size) {
                return -ENOSPC;
        }

        status = rlc_tx_submit(ctx, &chunk);
        if (status != 0) {
                return status;
        }

        return total_size;
}
