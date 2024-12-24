
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <rlc/rlc.h>
#include <rlc/chunks.h>

#include "utils.h"

static const char *rlc_transfer_type_str(enum rlc_transfer_type type)
{
        switch (type) {
        case RLC_AM:
                return "AM";
        case RLC_TM:
                return "TM";
        case RLC_UM:
                return "UM";
        }
}

static size_t header_size_(enum rlc_transfer_type type)
{
        switch (type) {
        case RLC_AM:
                return 3;
        case RLC_UM:
                return 1;
        case RLC_TM:
                return 1;
        }
}

static size_t pdu_calc_size_(struct rlc_context *ctx, size_t payload_size,
                             size_t max_size)
{
        /* TODO: calculate header size */
        return rlc_min(payload_size, max_size - header_size_(ctx->type));
}

static void encode_pdu_header_(struct rlc_context *ctx,
                               struct rlc_transfer *transfer,
                               struct rlc_pdu *pdu, struct rlc_chunk *chunk)
{
        /* TODO */
        chunk->data = "Test";
        chunk->size = 3;
}

static rlc_errno decode_pdu_(struct rlc_context *ctx, struct rlc_pdu *pdu,
                             struct rlc_chunk *chunks, size_t num_chunks)
{
        (void)ctx;
        (void)pdu;
        (void)chunks;
        (void)num_chunks;

        return 0;
}

static inline rlc_errno do_tx_request_(struct rlc_context *ctx)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->tx_request == NULL) {
                return -ENOTSUP;
        }

        return methods->tx_request(ctx);
}

static ssize_t do_tx_submit_(struct rlc_context *ctx,
                             struct rlc_transfer *transfer, struct rlc_pdu *pdu,
                             size_t max_size)
{
        ssize_t status;
        ssize_t total_size;
        size_t num_chunks;
        const struct rlc_methods *methods;

        methods = ctx->methods;
        if (methods->tx_submit == NULL) {
                return -ENOTSUP;
        }

        pdu->sn = transfer->sn;

        {
                /* Allocate and (shallow) copy over the chunks of the SDU,
                 * adding the header at the start. */
                num_chunks = rlc_chunks_num_view(transfer->chunks,
                                                 transfer->num_chunks,
                                                 pdu->size, pdu->seg_offset);
                num_chunks += 1; /* PDU header is kept in chunk 0 */
                struct rlc_chunk chunks[num_chunks];

                status = rlc_chunks_copy_view(transfer->chunks,
                                              transfer->num_chunks, chunks + 1,
                                              pdu->size, pdu->seg_offset);
                if (status != pdu->size) {
                        if (status >= 0) {
                                status = -ENODATA;
                        }

                        rlc_errf("Chunk copy failed: %" RLC_PRI_ERRNO,
                                 (rlc_errno)status);
                        return status;
                }

                encode_pdu_header_(ctx, transfer, pdu, &chunks[0]);
                total_size = rlc_chunks_size(chunks, num_chunks);

                if (total_size > max_size) {
                        return -ENOSPC;
                }

                status = methods->tx_submit(ctx, chunks, num_chunks);
                if (status != 0) {
                        return status;
                }
        }

        return total_size;
}

static inline void *do_alloc_(struct rlc_context *ctx, size_t size)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->mem_alloc == NULL) {
                return NULL;
        }

        return methods->mem_alloc(ctx, size);
}

static inline void do_dealloc_(struct rlc_context *ctx, void *mem)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->mem_dealloc == NULL) {
                return;
        }

        methods->mem_dealloc(ctx, mem);
}

static void append_transfer_(struct rlc_context *ctx,
                             struct rlc_transfer *transfer)
{
        struct rlc_transfer *cur;
        struct rlc_transfer **lastp;

        lastp = &ctx->transfers;

        for (rlc_each_node(ctx->transfers, cur, next)) {
                lastp = &cur->next;
        }

        *lastp = transfer;
}

static struct rlc_transfer *transfer_alloc_(struct rlc_context *ctx,
                                            enum rlc_transfer_dir dir)
{
        struct rlc_transfer *transfer;

        transfer = do_alloc_(ctx, sizeof(*transfer));
        if (transfer == NULL) {
                return NULL;
        }

        (void)memset(transfer, 0, sizeof(*transfer));

        transfer->dir = dir;

        if (transfer->dir == RLC_RX) {
                transfer->rx_buffer = do_alloc_(ctx, ctx->buffer_size);
                if (transfer->rx_buffer == NULL) {
                        do_dealloc_(ctx, transfer);
                        return NULL;
                }

                transfer->rx_buffer_size = ctx->buffer_size;
        }

        return transfer;
}

static void transfer_dealloc_(struct rlc_context *ctx,
                              struct rlc_transfer *transfer)
{
        if (transfer->dir == RLC_RX) {
                do_dealloc_(ctx, transfer->rx_buffer);
        }

        do_dealloc_(ctx, transfer);
}

static void prepare_pdu_(struct rlc_context *ctx, struct rlc_pdu *pdu)
{
        (void)memset(pdu, 0, sizeof(*pdu));

        pdu->type = ctx->type;
}

rlc_errno rlc_init(struct rlc_context *ctx, enum rlc_transfer_type type,
                   size_t window_size, size_t buffer_size,
                   const struct rlc_methods *methods)
{
        (void)memset(ctx, 0, sizeof(*ctx));

        ctx->type = type;
        ctx->methods = methods;
        ctx->window_size = window_size;
        ctx->buffer_size = buffer_size;

        ctx->workbuf.size = header_size_(type);

        ctx->workbuf.mem = do_alloc_(ctx, ctx->workbuf.size);
        if (ctx->workbuf.mem == NULL) {
                return -ENOMEM;
        }

        return 0;
}

rlc_errno rlc_send(struct rlc_context *ctx, struct rlc_transfer *transfer,
                   const struct rlc_chunk *chunks, size_t num_chunks)
{
        /* Encode chunk in a transfer */
        (void)memset(transfer, 0, sizeof(*transfer));

        transfer->dir = RLC_TX;
        transfer->chunks = chunks;
        transfer->num_chunks = num_chunks;
        transfer->sn = ctx->tx_next++;

        append_transfer_(ctx, transfer);

        return do_tx_request_(ctx);
}

/**
 * @details
 * Submits the incoming packet into the RLC system
 */
void rlc_rx_submit(struct rlc_context *ctx, struct rlc_chunk *chunks,
                   size_t num_chunks)
{
        rlc_errno status;
        struct rlc_pdu pdu;
        struct rlc_transfer *transfer;
        struct rlc_chunk *cur_chunk;

        status = decode_pdu_(ctx, &pdu, chunks, num_chunks);
        if (status != 0) {
                rlc_errf("Unable to decode PDU; dropping");
                return;
        }

        if (pdu.type != ctx->type) {
                rlc_errf("CTX/PDU type mismatch: %s vs %s",
                         rlc_transfer_type_str(pdu.type),
                         rlc_transfer_type_str(ctx->type));
                return;
        }

        /* Find assigned transfer */
        for (rlc_each_node(ctx->transfers, transfer, next)) {
                if (transfer->dir == RLC_RX && transfer->sn == pdu.sn) {
                        break;
                }
        }

        if (transfer == NULL) {
                if (pdu.seg_offset != 0) {
                        rlc_errf("Unrecognized SN; dropping");
                        return;
                }

                transfer = transfer_alloc_(ctx, RLC_RX);
                if (transfer == NULL) {
                        rlc_errf("SDU alloc failed");
                        return;
                }

                transfer->state = RLC_READY;
                transfer->sn = pdu.sn;

                /* TODO: check if full SDU (no SN) */

                append_transfer_(ctx, transfer);
        }

        for (rlc_each_item(chunks, cur_chunk, num_chunks)) {
        }

        /* TODO: if last, deliver upper layer */
}

void rlc_tx_avail(struct rlc_context *ctx, size_t size)
{
        ssize_t ret;
        size_t tot_size;
        size_t pdu_size;
        const void *data;
        struct rlc_pdu pdu;
        struct rlc_transfer *cur;

        for (rlc_each_node(ctx->transfers, cur, next)) {
                prepare_pdu_(ctx, &pdu);

                if (cur->dir == RLC_RX) {
                        if (ctx->type != RLC_AM || cur->state != RLC_DOACK) {
                                continue;
                        }

                        /* Set offset to ack */
                } else {
                        switch (cur->state) {
                        case RLC_READY:
                                tot_size = rlc_chunks_size(cur->chunks,
                                                           cur->num_chunks);
                                pdu_size = tot_size - cur->tx_offset_unack;

                                pdu.seg_offset = cur->tx_offset_unack;
                                pdu.size = pdu_calc_size_(ctx, pdu_size, size);

                                cur->tx_offset_unack += pdu.size;
                                if (cur->tx_offset_unack >= tot_size) {
                                        cur->state = RLC_WAITACK;
                                }

                                break;
                        case RLC_RESEND:
                                rlc_assert(ctx->type == RLC_AM);

                                tot_size = cur->tx_offset_unack -
                                           cur->tx_offset_ack;

                                pdu.seg_offset = cur->tx_offset_ack;
                                pdu.size = pdu_calc_size_(ctx, tot_size, size);

                                break;
                        default:
                                continue;
                        }
                }

                ret = do_tx_submit_(ctx, cur, &pdu, size);
                if (ret <= 0) { /* TODO: log err */
                        rlc_errf("PDU submit failed: error %" RLC_PRI_ERRNO,
                                 (rlc_errno)ret);
                        return;
                }

                size -= ret;
                if (size == 0) {
                        break;
                }
        }
}
