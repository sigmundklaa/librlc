
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>

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

static ssize_t tx_pdu_view_(struct rlc_context *ctx, struct rlc_pdu *pdu,
                            struct rlc_chunk *payload, size_t max_size)
{
        size_t num_chunks;

        num_chunks = rlc_chunks_num_view(payload, pdu->size, pdu->seg_offset);

        {
                struct rlc_chunk chunks[num_chunks];

                (void)rlc_chunks_copy_view(payload, chunks, pdu->size,
                                           pdu->seg_offset);
                return do_tx_submit_(ctx, pdu, chunks, max_size);
        }
}

static inline void *do_alloc_(struct rlc_context *ctx, size_t size)
{
        const struct rlc_methods *methods = ctx->methods;
        void *mem;
        if (methods->mem_alloc == NULL) {
                return NULL;
        }

        mem = methods->mem_alloc(ctx, size);
        (void)memset(mem, 0, size);

        return mem;
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

                lastp = &sdu->next;
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
                sdu->rx_buffer = do_alloc_(ctx, ctx->conf->buffer_size);
                if (sdu->rx_buffer == NULL) {
                        do_dealloc_(ctx, sdu);
                        return NULL;
                }

                sdu->rx_buffer_size = ctx->conf->buffer_size;
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
        struct rlc_sdu_segment *seg;

        sdu_dealloc_rx_buffer_(ctx, sdu);

        for (rlc_each_node_safe(struct rlc_sdu_segment, sdu->segments, seg,
                                next)) {
                do_dealloc_(ctx, seg);
        }

        do_dealloc_(ctx, sdu);
}

static void prepare_pdu_(const struct rlc_context *ctx, struct rlc_pdu *pdu)
{
        (void)memset(pdu, 0, sizeof(*pdu));
}

rlc_errno rlc_init(struct rlc_context *ctx, enum rlc_sdu_type type,
                   const struct rlc_config *config,
                   const struct rlc_methods *methods)
{
        (void)memset(ctx, 0, sizeof(*ctx));

        ctx->type = type;
        ctx->methods = methods;
        ctx->conf = config;

        return 0;
}

static bool seg_overlap_(const struct rlc_segment *left,
                         const struct rlc_segment *right)
{
        return right->start >= left->start && right->start <= left->end;
}

static rlc_errno seg_append_(struct rlc_context *ctx, struct rlc_sdu *sdu,
                             struct rlc_segment seg)
{
        struct rlc_sdu_segment *new_seg;
        struct rlc_sdu_segment *cur;
        struct rlc_sdu_segment *next;
        struct rlc_sdu_segment **lastp;

        lastp = &sdu->segments;
        next = NULL;

        for (rlc_each_node(sdu->segments, cur, next)) {
                next = cur->next;

                /* Seg overlaps with the element to the left */
                if (seg_overlap_(&cur->seg, &seg)) {
                        /* Overlaps to the right. Since overlap on both sides,
                         * they can be merged. */
                        if (next != NULL && seg_overlap_(&seg, &next->seg)) {
                                seg.end = next->seg.end;

                                cur->next = next->next;
                                do_dealloc_(ctx, next);
                        }

                        seg.start = cur->seg.start;
                        goto entry_found;
                }

                /* If not overlapping to the left, but the offset is higher than
                 * that of the segment to the left */
                if (seg.start > cur->seg.end) {
                        /* Overlap to the right, merge */
                        if (next != NULL && seg.end >= next->seg.start) {
                                cur = next;
                                seg.end = next->seg.end;

                                goto entry_found;
                        }

                        /* Otherwise create a new entry and insert it */
                        lastp = &cur->next;
                        break;
                } else if (seg.start < cur->seg.start) {
                        if (seg.end >= cur->seg.start) {
                                seg.end = cur->seg.end;

                                goto entry_found;
                        }

                        /* Insert before current */
                        next = cur;
                        break;
                }

                lastp = &cur->next;
        }

        cur = do_alloc_(ctx, sizeof(*cur));
        if (cur == NULL) {
                return -ENOMEM;
        }

        *lastp = cur;
        cur->next = next;

entry_found:
        (void)memcpy(&cur->seg, &seg, sizeof(seg));

        return 0;
}

rlc_errno rlc_send(struct rlc_context *ctx, struct rlc_chunk *chunks)
{
        rlc_errno status;
        struct rlc_segment seg;
        struct rlc_sdu *sdu;

        sdu = do_alloc_(ctx, sizeof(*sdu));
        if (sdu == NULL) {
                return -ENOMEM;
        }

        sdu->dir = RLC_TX;
        sdu->chunks = chunks;
        sdu->sn = ctx->tx.next++;

        append_sdu_(ctx, sdu);

        seg.start = 0;
        seg.end = rlc_chunks_size(chunks);

        rlc_dbgf("TX; Queueing SDU %" PRIu32 ", RANGE: %" PRIu32 "->%" PRIu32,
                 sdu->sn, seg.start, seg.end);

        status = seg_append_(ctx, sdu, seg);
        if (status != 0) {
                return status;
        }

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

        rlc_inff("RX; SDU %" PRIu32 " received (%" PRIu32 "B)", sdu->sn,
                 sdu->segments->seg.end);

        event.type = RLC_EVENT_RX_DONE;
        event.data.rx_done.data = sdu->rx_buffer;
        event.data.rx_done.size = sdu->segments->seg.end;

        do_event_(ctx, &event);
}

static void do_rx_done_direct_(struct rlc_context *ctx,
                               const struct rlc_chunk *chunks)
{
        struct rlc_event event;

        rlc_inff("RX; Full SDU delivered (%zuB)", rlc_chunks_size(chunks));

        event.type = RLC_EVENT_RX_DONE;
        event.data.rx_done = *chunks;

        do_event_(ctx, &event);
}

static void do_tx_done_(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_event event;

        rlc_inff("TX; SDU %" PRIu32 " transmitted (%zuB)", sdu->sn,
                 rlc_chunks_size(sdu->chunks));

        event.type = RLC_EVENT_TX_DONE;
        event.data.tx_done = sdu->chunks;

        do_event_(ctx, &event);
}

static void am_tx_next_ack_update_(struct rlc_context *ctx, uint16_t sn)
{
        struct rlc_sdu *sdu;
        struct rlc_sdu *next;
        struct rlc_sdu **lastp;
        struct rlc_sdu **tmp;
        uint32_t lowest;

        if (ctx->tx.next_ack >= sn) {
                return;
        }

        rlc_dbgf("TX AM STATUS; ACK_SN: %" PRIu32, sn);

        lastp = &ctx->sdus;
        lowest = ctx->tx.next;
        next = NULL;

        for (sdu = ctx->sdus; sdu != NULL; sdu = next) {
                /* Can't do standard iteration since the SDU can be deallocated
                 * while iterating, so attempting to access sdu->next would be
                 * undefined behaviour after that. */
                next = sdu->next;

                if (sdu->dir == RLC_TX) {
                        if (sdu->sn < sn) {
                                do_tx_done_(ctx, sdu);
                                sdu_dealloc_(ctx, sdu);

                                *lastp = next;

                                continue;
                        } else if (sdu->sn < lowest) {
                                /* Set to the lowest non-acked SN */
                                lowest = sdu->sn;
                        }
                }

                lastp = &sdu->next;
        }

        ctx->tx.next_ack = lowest;
        rlc_dbgf("TX AM: TX_NEXT_ACK=%" PRIu32, ctx->tx.next_ack);
}

static void process_status_(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                            const struct rlc_chunk *chunks)
{
        ssize_t bytes;
        size_t offset;
        rlc_errno status;
        struct rlc_pdu_status cur;
        struct rlc_sdu *sdu;

        offset = rlc_pdu_header_size(ctx, pdu) - 1;

        am_tx_next_ack_update_(ctx, pdu->sn);

        /* Iterate over every status */
        bytes = rlc_status_decode(ctx, &cur, chunks, offset);
        while (bytes > 0) {
                /* register rx_next -> tx_next */
                /* update nacked of every tx sdu */
                rlc_dbgf("TX AM STATUS; NACK_SN: %" PRIu32 ", RANGE: %" PRIu32
                         "->%" PRIu32,
                         cur.nack_sn, cur.offset.start, cur.offset.end);
                if (!cur.ext.has_offset) {
                        continue;
                }

                for (rlc_each_node(ctx->sdus, sdu, next)) {
                        if (sdu->dir == RLC_TX && sdu->sn == cur.nack_sn) {
                                break;
                        }
                }

                if (sdu == NULL) {
                        rlc_errf("Unrecognized SN: %u", cur.nack_sn);
                        continue;
                }

                if (cur.offset.end == RLC_STATUS_SO_MAX) {
                        cur.offset.end = rlc_chunks_size(sdu->chunks);
                }

                status = seg_append_(ctx, sdu, cur.offset);
                if (status != 0) {
                        rlc_errf("TX AM STATUS; Unable to append seg "
                                 "(%" RLC_PRI_ERRNO ")",
                                 status);
                        return;
                }

                sdu->state = RLC_READY;

                offset += bytes;
                bytes = rlc_status_decode(ctx, &cur, chunks, offset);
        }

        if (bytes < 0) {
                rlc_errf("TX AM STATUS; Decode failed: %" RLC_PRI_ERRNO,
                         (rlc_errno)bytes);
                return;
        }
}

static bool is_rx_done_(struct rlc_sdu *sdu)
{
        /* Last received and exactly one segment */
        return sdu->flags.rx_last_received && sdu->segments != NULL &&
               sdu->segments->seg.start == 0 && sdu->segments->next == NULL;
}

static bool in_window_(uint32_t sn, uint32_t base, uint32_t size)
{
        return base <= sn && sn < base + size;
}

static uint32_t lowest_sn_(struct rlc_context *ctx, enum rlc_sdu_dir dir)
{
        uint32_t lowest;
        struct rlc_sdu *cur;

        lowest = UINT32_MAX;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (cur->sn < lowest) {
                        lowest = cur->sn;
                }
        }

        return lowest;
}

/**
 * @details
 * Submits the incoming packet into the RLC system
 */
void rlc_rx_submit(struct rlc_context *ctx, const struct rlc_chunk *chunks)
{
        ssize_t status;
        size_t header_size;
        uint32_t lowest;
        struct rlc_pdu pdu;
        struct rlc_sdu *sdu;
        struct rlc_chunk *cur_chunk;

        status = rlc_pdu_decode(ctx, &pdu, chunks);
        if (status != 0) {
                return;
        }

        if (pdu.flags.is_status) {
                process_status_(ctx, &pdu, chunks);

                (void)do_tx_request_(ctx);
                return;
        }

        if (ctx->type == RLC_TM || (pdu.flags.is_first && pdu.flags.is_last)) {
                do_rx_done_direct_(ctx, chunks);

                return;
        } else if (!in_window_(pdu.sn, ctx->rx.next, ctx->conf->window_size)) {
                rlc_errf("RX; SN %" PRIu32 " outside RX window (%" PRIu32
                         "->%zu), dropping",
                         pdu.sn, ctx->rx.next,
                         ctx->rx.next + ctx->conf->window_size);
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
                        rlc_errf("RX; Unrecognized SN %" PRIu32 ", dropping",
                                 pdu.sn);
                        return;
                }

                sdu = sdu_alloc_(ctx, RLC_RX);
                if (sdu == NULL) {
                        rlc_errf("RX; SDU alloc failed (%" RLC_PRI_ERRNO ")",
                                 -ENOMEM);
                        return;
                }

                sdu->state = RLC_READY;
                sdu->sn = pdu.sn;

                append_sdu_(ctx, sdu);
        }

        if (sdu->state != RLC_READY) {
                rlc_errf("RX; Received when not ready, discarding");
                return;
        }

        if (pdu.seg_offset >= sdu->rx_buffer_size) {
                rlc_errf("RX; Offset out of bounds: %" PRIu32 ">%zu",
                         pdu.seg_offset, sdu->rx_buffer_size);
                return;
        }

        header_size = rlc_pdu_header_size(ctx, &pdu);

        /* Copy the contents of the chunks, skipping the header content */
        status = rlc_chunks_deepcopy_view(
                chunks, (uint8_t *)sdu->rx_buffer + pdu.seg_offset,
                sdu->rx_buffer_size - pdu.seg_offset, header_size);
        if (status <= 0) {
                rlc_errf("RX; Unable to flatten chunks: (%" RLC_PRI_ERRNO ")",
                         (rlc_errno)status);
                return;
        }

        status = seg_append_(ctx, sdu,
                             (struct rlc_segment){
                                     .start = pdu.seg_offset,
                                     .end = pdu.seg_offset +
                                            rlc_chunks_size(chunks) -
                                            header_size,
                             });
        if (status != 0) {
                rlc_errf("RX; Unable to append segment (%" RLC_PRI_ERRNO ")",
                         (rlc_errno)status);
                return;
        }

        if (pdu.flags.is_last) {
                sdu->flags.rx_last_received = 1;
        }

        if (sdu->sn >= ctx->rx.next_highest) {
                ctx->rx.next_highest = sdu->sn + 1;
        }

        if (is_rx_done_(sdu)) {
                do_rx_done_(ctx, sdu);
                remove_sdu_(ctx, sdu);

                /* In acknowledged mode, the SDU should be deallocated when
                 * transmitting the status, so that we can keep the information
                 * in memory until it can be relayed back. The RX buffer can
                 * however be freed to prevent excessive memory use. */
                if (ctx->type == RLC_AM) {
                        ctx->gen_status = true;
                        lowest = rlc_min(lowest_sn_(ctx, RLC_RX),
                                         ctx->rx.next_highest);

                        if (sdu->sn == ctx->rx.highest_status) {
                                ctx->rx.highest_status = lowest;
                        }

                        if (sdu->sn == ctx->rx.next) {
                                ctx->rx.next = lowest;
                        }
                }

                sdu_dealloc_(ctx, sdu);
        }

        if (ctx->type == RLC_AM && pdu.flags.polled) {
                /* Send ACK before receiving any more data */
                ctx->gen_status = true;
        }

        (void)do_tx_request_(ctx);
}

static void pdu_size_adjust_(const struct rlc_context *ctx, struct rlc_pdu *pdu,
                             size_t max_size)
{
        size_t hsize;

        if (ctx->type == RLC_UM && pdu->flags.is_first) {
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

/**
 * @brief Determine if the next PDU for @p sdu should poll
 *
 * Returns true if:
 * - Type == AM and
 *   - Retransmitted (not the last segment in the list)
 *
 * @param ctx Context
 * @param sdu
 * @return bool
 * @retval true Poll
 * @retval false Don't poll
 */
static bool should_poll_(const struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        return ctx->type == RLC_AM &&
               (sdu->next != NULL ||
                ctx->tx.pdu_without_poll >= ctx->conf->pdu_without_poll_max ||
                ctx->tx.byte_without_poll >= ctx->conf->byte_without_poll_max);
}

static bool serve_sdu_(struct rlc_context *ctx, struct rlc_sdu *sdu,
                       struct rlc_pdu *pdu, size_t size_avail)
{
        size_t tot_size;
        struct rlc_sdu_segment *segment;

        switch (sdu->state) {
        case RLC_READY:
                segment = sdu->segments;
                if (segment == NULL) {
                        sdu->state = RLC_WAIT;
                        break;
                }

                pdu->sn = sdu->sn;
                pdu->size = segment->seg.end - segment->seg.start;
                if (pdu->size == 0) {
                        rlc_assert(ctx->type == RLC_AM);

                        sdu->state = RLC_WAIT;
                        return false;
                }

                pdu->seg_offset = segment->seg.start;
                pdu->flags.is_first = pdu->seg_offset == 0;

                pdu_size_adjust_(ctx, pdu, size_avail);

                segment->seg.start += pdu->size;
                /* Segment done */
                if (segment->seg.start >= segment->seg.end) {
                        /* If last segment, set last flag and go into waiting
                         * state. The last segment is kept alive until the
                         * SDU is deallocated, so that it can be used to
                         * distuingish between retransmitted PDUs and
                         * first-time-transmitted PDUs. */
                        if (segment->next == NULL) {
                                sdu->state = RLC_WAIT;
                                pdu->flags.is_last = 1;
                        } else {
                                sdu->segments = segment->next;

                                do_dealloc_(ctx, segment);
                        }
                }

                ctx->tx.pdu_without_poll += 1;
                ctx->tx.byte_without_poll += pdu->size;

                pdu->flags.polled = should_poll_(ctx, sdu);
                if (pdu->flags.polled) {
                        ctx->tx.pdu_without_poll = 0;
                        ctx->tx.byte_without_poll = 0;

                        rlc_dbgf("TX; Polling %" PRIu32 " for status", pdu->sn);
                }

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
        size_t size;
        ssize_t ret;

        size = rlc_status_size(ctx, status);

        chunk = do_alloc_(ctx, sizeof(*chunk) + size);
        if (chunk == NULL) {
                return NULL;
        }

        (void)memset(chunk, 0, sizeof(*chunk) + size);

        /* Initialize chunk and add to the list */
        chunk->data = chunk + 1;
        chunk->next = NULL;

        rlc_status_encode(ctx, status, chunk);

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

static void log_rx_status_(struct rlc_pdu_status *status)
{
        rlc_dbgf("RX AM STATUS; Detected missing; SN: "
                 "%" PRIu32 ", RANGE:  %" PRIu32 "->%" PRIu32,
                 status->nack_sn, status->offset.start, status->offset.end);
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
        struct rlc_sdu_segment *seg;
        size_t count;
        size_t status_idx;

        prepare_pdu_(ctx, &pdu);
        pdu.flags.is_status = 1;
        pdu.sn = ctx->rx.highest_status;

        count = 0;
        status_idx = 0;
        head_chunk = NULL;
        chunkptr = &head_chunk;

        last_status = NULL;

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_RX) {
                        continue;
                }

                for (rlc_each_node(sdu->segments, seg, next)) {
                        /* No more missing segments */
                        if (seg->next == NULL && sdu->flags.rx_last_received) {
                                break;
                        }

                        /* Alternate between two allocated status structs, so
                         * that we can fill the current one and encode the last
                         * one */
                        status_idx = 1 - status_idx;
                        cur_status = &status_pool[status_idx];

                        *cur_status = (struct rlc_pdu_status){
                                .ext.has_offset = 1,
                                .nack_sn = sdu->sn,
                        };

                        if (seg->next != NULL) {
                                /* Between two segments */
                                cur_status->offset.start = seg->seg.end;
                                cur_status->offset.end = seg->next->seg.start;
                        } else {
                                /* Last segment registered, but last segment of
                                 * the transmission has not been received */
                                cur_status->offset.start = seg->seg.end;
                                cur_status->offset.end = RLC_STATUS_SO_MAX;
                        }

                        /* Encode the last status instead of the current one, so
                         * that the E1 bit can be set appropriately. On the
                         * first iteration, skip encoding as there is no last */
                        if (last_status != NULL) {
                                last_status->ext.has_more = 1;

                                log_rx_status_(last_status);

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
        }

        if (count > 0) {
                log_rx_status_(last_status);

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
        struct rlc_sdu *tmp;

        if (ctx->type == RLC_AM && should_gen_status_(ctx)) {
                (void)gen_status_(ctx, size);
                return;
        }

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (cur->dir != RLC_TX) {
                        continue;
                }

                prepare_pdu_(ctx, &pdu);

                if (!serve_sdu_(ctx, cur, &pdu, size)) {
                        continue;
                }

                rlc_dbgf("TX PDU; SN: %" PRIu32 ", range: %" PRIu32 "->"
                         "%zu",
                         pdu.sn, pdu.seg_offset, pdu.seg_offset + pdu.size);

                ret = tx_pdu_view_(ctx, &pdu, cur->chunks, size);
                if (ret <= 0) {
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
