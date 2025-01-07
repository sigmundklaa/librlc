
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <rlc/rlc.h>
#include <rlc/chunks.h>

#include "utils.h"
#include "encode.h"

static const char *rlc_sdu_type_str(enum rlc_sdu_type type)
{
        switch (type) {
        case RLC_AM:
                return "AM";
        case RLC_TM:
                return "TM";
        case RLC_UM:
                return "UM";
        default:
                rlc_assert(0);
                return NULL;
        }
}

static inline rlc_errno do_tx_request_(struct rlc_context *ctx)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->tx_request == NULL) {
                return -ENOTSUP;
        }

        return methods->tx_request(ctx);
}

static ssize_t do_tx_submit_(struct rlc_context *ctx, struct rlc_pdu *pdu,
                             struct rlc_chunk *payload, size_t max_size)
{
        ssize_t status;
        ssize_t total_size;
        uint8_t header[rlc_pdu_header_size(ctx, pdu)];
        struct rlc_chunk chunk;
        const struct rlc_methods *methods;

        methods = ctx->methods;
        if (methods->tx_submit == NULL) {
                return -ENOTSUP;
        }

        /* Size is set by encode */
        chunk.data = header;
        chunk.next = payload;

        (void)memset(header, 0, sizeof(header));

        rlc_pdu_encode(ctx, pdu, &chunk);
        total_size = rlc_chunks_size(&chunk);

        if (total_size > max_size) {
                return -ENOSPC;
        }

        status = methods->tx_submit(ctx, &chunk);
        if (status != 0) {
                return status;
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

static void append_sdu_(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        struct rlc_sdu **lastp;

        lastp = &ctx->sdus;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                lastp = &cur->next;
        }

        *lastp = sdu;
}

static void remove_sdu_(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        struct rlc_sdu **lastp;

        lastp = &ctx->sdus;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (cur == sdu) {
                        *lastp = sdu->next;

                        sdu->next = NULL;
                        break;
                }

                lastp = &cur;
        }
}

static struct rlc_sdu *sdu_alloc_(struct rlc_context *ctx, enum rlc_sdu_dir dir)
{
        struct rlc_sdu *sdu;

        sdu = do_alloc_(ctx, sizeof(*sdu));
        if (sdu == NULL) {
                return NULL;
        }

        (void)memset(sdu, 0, sizeof(*sdu));

        sdu->dir = dir;

        if (sdu->dir == RLC_RX) {
                sdu->rx_buffer = do_alloc_(ctx, ctx->buffer_size);
                if (sdu->rx_buffer == NULL) {
                        do_dealloc_(ctx, sdu);
                        return NULL;
                }

                sdu->rx_buffer_size = ctx->buffer_size;
        }

        return sdu;
}

static void sdu_dealloc_rx_buffer_(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        if (sdu->dir != RLC_RX || sdu->rx_buffer == NULL) {
                return;
        }

        do_dealloc_(ctx, sdu->rx_buffer);
        sdu->rx_buffer = NULL;
}

static void sdu_dealloc_(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        sdu_dealloc_rx_buffer_(ctx, sdu);
        do_dealloc_(ctx, sdu);
}

static void prepare_pdu_(const struct rlc_context *ctx, struct rlc_pdu *pdu)
{
        (void)memset(pdu, 0, sizeof(*pdu));

        pdu->type = ctx->type;
}

rlc_errno rlc_init(struct rlc_context *ctx, enum rlc_sdu_type type,
                   size_t window_size, size_t buffer_size,
                   const struct rlc_methods *methods)
{
        (void)memset(ctx, 0, sizeof(*ctx));

        ctx->type = type;
        ctx->methods = methods;
        ctx->window_size = window_size;
        ctx->buffer_size = buffer_size;
        ctx->sn_width = RLC_SN_12BIT;

        return 0;
}

rlc_errno rlc_send(struct rlc_context *ctx, struct rlc_sdu *sdu,
                   struct rlc_chunk *chunks)
{
        /* Encode chunk in a sdu */
        (void)memset(sdu, 0, sizeof(*sdu));

        sdu->dir = RLC_TX;
        sdu->chunks = chunks;
        sdu->sn = ctx->tx_next++;

        append_sdu_(ctx, sdu);

        return do_tx_request_(ctx);
}

static void do_event_(struct rlc_context *ctx, struct rlc_event *event)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->event == NULL) {
                rlc_assert(0);
                return;
        }

        methods->event(ctx, event);
}

static void do_rx_done_(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        event.type = RLC_EVENT_RX_DONE;
        event.data.rx_done.data = sdu->rx_buffer;
        event.data.rx_done.size = sdu->rx_pos;

        do_event_(ctx, &event);
}

static void process_status_(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                            const struct rlc_chunk *chunks)
{
        ssize_t bytes;
        size_t offset;
        struct rlc_pdu_status cur;

        offset = rlc_pdu_header_size(ctx, pdu);

        /* Iterate over every status */
        bytes = rlc_status_decode(ctx, &cur, chunks, offset);
        for (; bytes > 0; offset += bytes) {
                /* register rx_next -> tx_next */
                /* update nacked of every tx sdu */

                bytes = rlc_status_decode(ctx, &cur, chunks, offset);
        }

        if (bytes < 0) {
                rlc_errf("Status decode failed: %" RLC_PRI_ERRNO,
                         (rlc_errno)bytes);
                return;
        }
}

/**
 * @details
 * Submits the incoming packet into the RLC system
 */
void rlc_rx_submit(struct rlc_context *ctx, const struct rlc_chunk *chunks)
{
        ssize_t status;
        struct rlc_pdu pdu;
        struct rlc_sdu *sdu;
        struct rlc_chunk *cur_chunk;

        pdu.type = ctx->type;

        status = rlc_pdu_decode(ctx, &pdu, chunks);
        if (status != 0) {
                process_status_(ctx, &pdu, chunks);
                return;
        }

        if (pdu.flags.is_status) {
                rlc_errf("Status");
                return;
        }

        /* Find assigned sdu */
        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_RX && sdu->sn == pdu.sn) {
                        break;
                }
        }

        if (sdu == NULL) {
                if (!pdu.flags.is_first) {
                        rlc_errf("Unrecognized SN; dropping");
                        return;
                }

                sdu = sdu_alloc_(ctx, RLC_RX);
                if (sdu == NULL) {
                        rlc_errf("SDU alloc failed");
                        return;
                }

                sdu->state = RLC_READY;
                sdu->sn = pdu.sn;

                append_sdu_(ctx, sdu);
        }

        if (sdu->state != RLC_READY) {
                rlc_errf("Received when non-ready; discarding");
                return;
        }

        if (!pdu.flags.is_first && pdu.seg_offset >= sdu->rx_buffer_size) {
                rlc_errf("Out of bounds: %i", (int)pdu.seg_offset);
                return;
        }

        /* Copy the contents of the chunks, skipping the header content */
        status = rlc_chunks_deepcopy_view(
                chunks, (uint8_t *)sdu->rx_buffer + pdu.seg_offset,
                sdu->rx_buffer_size - pdu.seg_offset,
                rlc_pdu_header_size(ctx, &pdu));
        if (status <= 0) {
                rlc_errf("Chunk deepcopy failed: %" RLC_PRI_ERRNO,
                         (rlc_errno)status);
                return;
        }

        sdu->rx_pos += status;

        if (pdu.flags.is_last) {
                do_rx_done_(ctx, sdu);

                /* In acknowledged mode, the SDU should be deallocated when
                 * transmitting the status, so that we can keep the information
                 * in memory until it can be relayed back. The RX buffer can
                 * however be freed to prevent excessive memory use. */
                if (pdu.type == RLC_AM) {
                        ctx->gen_status = true;
                        ctx->rx_next_highest += 1;
                }

                remove_sdu_(ctx, sdu);
                sdu_dealloc_(ctx, sdu);
        } else if (pdu.type == RLC_AM && pdu.flags.polled) {
                /* Send ACK before receiving any more data */
                ctx->gen_status = true;
        }
}

static void pdu_size_adjust_(const struct rlc_context *ctx, struct rlc_pdu *pdu,
                             size_t max_size)
{
        size_t hsize;

        if (pdu->type == RLC_UM && pdu->flags.is_first) {
                /* If size plus the header can be fit as is both SN and SO can
                 * be omitted */
                if (max_size - 1 >= pdu->size) {
                        pdu->flags.is_last = 1;
                        return;
                }
        }

        hsize = rlc_pdu_header_size(ctx, pdu);
        if (pdu->size + hsize > max_size) {
                pdu->size -= pdu->size + hsize - max_size;
        }
}

static bool should_gen_status_(const struct rlc_context *ctx)
{
        return ctx->gen_status;
}

static ssize_t tx_sdu_(struct rlc_context *ctx, struct rlc_sdu *sdu,
                       struct rlc_pdu *pdu, size_t max_size)
{
        pdu->sn = sdu->sn;

        return do_tx_submit_(ctx, pdu, sdu->chunks, max_size);
}

static bool serve_sdu_(const struct rlc_context *ctx, struct rlc_sdu *sdu,
                       struct rlc_pdu *pdu, size_t size_avail)
{
        size_t tot_size;

        switch (sdu->state) {
        case RLC_READY:
                tot_size = rlc_chunks_size(sdu->chunks);

                pdu->size = tot_size - sdu->tx_offset_unack;
                pdu->seg_offset = sdu->tx_offset_unack;
                pdu->flags.is_first = pdu->seg_offset == 0;

                pdu_size_adjust_(ctx, pdu, size_avail);

                sdu->tx_offset_unack += pdu->size;
                if (pdu->flags.is_last || sdu->tx_offset_unack >= tot_size) {
                        pdu->flags.is_last = 1;
                        sdu->state = RLC_WAITACK;
                }

                break;
        case RLC_RESEND:
                rlc_assert(ctx->type == RLC_AM);

                break;
        default:
                return false;
        }

        return true;
}

static struct rlc_chunk *encode_status_(struct rlc_context *ctx,
                                        struct rlc_pdu_status *status)
{
        struct rlc_chunk *chunk;

        /* TODO: size */
        chunk = do_alloc_(ctx, sizeof(*chunk) + 4);
        if (chunk == NULL) {
                return NULL;
        }

        /* Initialize chunk and add to the list */
        chunk->data = chunk + 1;
        chunk->size = 4;
        chunk->next = NULL;

        return chunk;
}

static void free_chunks_(struct rlc_context *ctx, struct rlc_chunk *chunks)
{
        struct rlc_chunk *cur;
        struct rlc_chunk *next;

        cur = chunks;

        while (cur != NULL) {
                next = cur->next;
                do_dealloc_(ctx, cur);
                cur = next;
        }
}

static ssize_t gen_status_(struct rlc_context *ctx, size_t max_size)
{
        ssize_t ret;
        struct rlc_pdu pdu;
        struct rlc_sdu *sdu;
        struct rlc_pdu_status status_pool[2];
        struct rlc_pdu_status *cur_status;
        struct rlc_pdu_status *last_status;
        struct rlc_chunk *chunk;
        struct rlc_chunk *head_chunk;
        struct rlc_chunk **chunkptr;
        size_t count;
        size_t status_idx;

        prepare_pdu_(ctx, &pdu);
        pdu.flags.is_status = 1;
        pdu.sn = ctx->rx_next_highest;

        count = 0;
        status_idx = 0;
        head_chunk = NULL;
        chunkptr = &head_chunk;

        last_status = NULL;

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_RX) {
                        continue;
                }

                /* Alternate between two allocated status structs, so that
                 * we can fill the current one and encode the last one */
                status_idx = 1 - status_idx;
                cur_status = &status_pool[status_idx];

                *cur_status = (struct rlc_pdu_status){
                        .ext.has_offset = 1,
                        .nack_sn = sdu->sn,
                        .offset.start = sdu->rx_pos,
                        .offset.end = RLC_STATUS_SO_MAX,
                };

                /* Encode the last status instead of the current one, so that
                 * the E1 bit can be set appropriately. On the first iteration,
                 * skip encoding as there is no last */
                if (last_status != NULL) {
                        last_status->ext.has_more = 1;

                        chunk = encode_status_(ctx, last_status);
                        if (chunk == NULL) {
                                ret = -ENOMEM;
                                goto exit;
                        }

                        *chunkptr = chunk;
                        chunkptr = &chunk->next;
                }

                last_status = cur_status;
                count += 1;
        }

        if (count > 0) {
                chunk = encode_status_(ctx, last_status);
                if (chunk == NULL) {
                        ret = -ENOMEM;
                        goto exit;
                }

                *chunkptr = chunk;
                chunkptr = &chunk->next;

                pdu.flags.ext = 1;
        }

        ctx->gen_status = false;

        ret = do_tx_submit_(ctx, &pdu, head_chunk, max_size);

exit:
        free_chunks_(ctx, head_chunk);

        return ret;
}

void rlc_tx_avail(struct rlc_context *ctx, size_t size)
{
        ssize_t ret;
        size_t tot_size;
        size_t pdu_size;
        const void *data;
        struct rlc_pdu pdu;
        struct rlc_sdu *cur;

        if (should_gen_status_(ctx)) {
                ret = gen_status_(ctx, size);
                if (ret < 0) {
                        return;
                }

                size -= ret;
        }

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (cur->dir != RLC_TX) {
                        continue;
                }

                prepare_pdu_(ctx, &pdu);

                if (!serve_sdu_(ctx, cur, &pdu, size)) {
                        continue;
                }

                pdu.sn = cur->sn;

                ret = do_tx_submit_(ctx, &pdu, cur->chunks, size);
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
