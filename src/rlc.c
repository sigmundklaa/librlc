
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

static ssize_t do_tx_submit_(struct rlc_context *ctx, struct rlc_sdu *sdu,
                             struct rlc_pdu *pdu, size_t max_size)
{
        ssize_t status;
        ssize_t total_size;
        size_t num_chunks;
        const struct rlc_methods *methods;

        methods = ctx->methods;
        if (methods->tx_submit == NULL) {
                return -ENOTSUP;
        }

        pdu->sn = sdu->sn;

        /* Allocate and (shallow) copy over the chunks of the SDU,
         * adding the header at the start. */
        num_chunks = rlc_chunks_num_view(sdu->chunks, sdu->num_chunks,
                                         pdu->size, pdu->seg_offset);
        num_chunks += 1; /* PDU header is kept in chunk 0 */

        {
                struct rlc_chunk chunks[num_chunks];
                uint8_t header[rlc_pdu_header_size(ctx, pdu)];

                status = rlc_chunks_copy_view(sdu->chunks, sdu->num_chunks,
                                              chunks + 1, pdu->size,
                                              pdu->seg_offset);
                if (status != pdu->size) {
                        if (status >= 0) {
                                status = -ENODATA;
                        }

                        rlc_errf("Chunk copy failed: %" RLC_PRI_ERRNO,
                                 (rlc_errno)status);
                        return status;
                }

                chunks[0].data = header;
                (void)memset(header, 0, sizeof(header));

                rlc_pdu_encode(ctx, pdu, &chunks[0]);
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

static void sdu_dealloc_(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        if (sdu->dir == RLC_RX) {
                do_dealloc_(ctx, sdu->rx_buffer);
        }

        do_dealloc_(ctx, sdu);
}

static void prepare_pdu_(struct rlc_context *ctx, struct rlc_pdu *pdu)
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

        return 0;
}

rlc_errno rlc_send(struct rlc_context *ctx, struct rlc_sdu *sdu,
                   const struct rlc_chunk *chunks, size_t num_chunks)
{
        /* Encode chunk in a sdu */
        (void)memset(sdu, 0, sizeof(*sdu));

        sdu->dir = RLC_TX;
        sdu->chunks = chunks;
        sdu->num_chunks = num_chunks;
        sdu->sn = ctx->tx_next++;

        append_sdu_(ctx, sdu);

        return do_tx_request_(ctx);
}

static void do_event_(const struct rlc_context *ctx, struct rlc_event *event)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->event == NULL) {
                rlc_assert(0);
                return;
        }

        methods->event(ctx, event);
}

static void do_rx_done_(const struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        event.type = RLC_EVENT_RX_DONE;
        event.data.rx_done.data = sdu->rx_buffer;
        event.data.rx_done.size = sdu->rx_pos;

        do_event_(ctx, &event);
}

/**
 * @details
 * Submits the incoming packet into the RLC system
 */
void rlc_rx_submit(struct rlc_context *ctx, struct rlc_chunk *chunks,
                   size_t num_chunks)
{
        ssize_t status;
        struct rlc_pdu pdu;
        struct rlc_sdu *sdu;
        struct rlc_chunk *cur_chunk;

        pdu.type = ctx->type;

        status = rlc_pdu_decode(ctx, &pdu, chunks, num_chunks);
        if (status != 0) {
                rlc_errf("Unable to decode PDU; dropping");
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

        if (!pdu.flags.is_first && pdu.seg_offset >= sdu->rx_buffer_size) {
                rlc_errf("Out of bounds: %i", (int)pdu.seg_offset);
                return;
        }

        /* Copy the contents of the chunks, skipping the header content */
        status = rlc_chunks_deepcopy_view(
                chunks, num_chunks, (uint8_t *)sdu->rx_buffer + pdu.seg_offset,
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

                remove_sdu_(ctx, sdu);
                sdu_dealloc_(ctx, sdu);
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

static bool serve_sdu_(const struct rlc_context *ctx, struct rlc_sdu *sdu,
                       struct rlc_pdu *pdu, size_t size_avail)
{
        size_t tot_size;

        switch (sdu->state) {
        case RLC_READY:
                tot_size = rlc_chunks_size(sdu->chunks, sdu->num_chunks);

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
#if 0
                rlc_assert(ctx->type == RLC_AM);

                tot_size = sdu->tx_offset_unack - sdu->tx_offset_ack;

                pdu.seg_offset = sdu->tx_offset_ack;
                pdu.size = 0;
#endif

                break;
        default:
                return false;
        }

        return true;
}

void rlc_tx_avail(struct rlc_context *ctx, size_t size)
{
        ssize_t ret;
        size_t tot_size;
        size_t pdu_size;
        const void *data;
        struct rlc_pdu pdu;
        struct rlc_sdu *cur;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                prepare_pdu_(ctx, &pdu);

                if (cur->dir == RLC_RX) {
                        if (ctx->type != RLC_AM || cur->state != RLC_DOACK) {
                                continue;
                        }

                        /* Set offset to ack */
                } else {
                        if (!serve_sdu_(ctx, cur, &pdu, size)) {
                                continue;
                        }
                }

                pdu.sn = cur->sn;

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
