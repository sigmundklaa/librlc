
#include <string.h>

#include <rlc/rlc.h>
#include <rlc/buf.h>

#include "backend.h"
#include "encode.h"
#include "event.h"
#include "sdu.h"

struct status_pool {
        struct rlc_pdu_status mem[2];
        size_t index;
        size_t alloc_count;
};

static struct rlc_pdu_status *status_get(struct status_pool *pool)
{
        return &pool->mem[pool->index];
}

static struct rlc_pdu_status *status_last(struct status_pool *pool)
{
        return &pool->mem[1 - pool->index];
}

static struct rlc_pdu_status *status_advance(struct status_pool *pool)
{
        pool->alloc_count++;
        pool->index = 1 - pool->index;
        return status_get(pool);
}

static size_t status_count(struct status_pool *pool)
{
        return pool->alloc_count;
}

static void alarm_poll_retransmit(rlc_timer timer, struct rlc_context *ctx)
{
        rlc_dbgf("Retransmitting poll");

        ctx->force_poll = true;

        (void)rlc_tx_request(ctx);
}

static void log_rx_status(struct rlc_pdu_status *status)
{
        rlc_dbgf("RX AM STATUS; Detected missing; SN: "
                 "%" PRIu32 ", RANGE:  %" PRIu32 "->%" PRIu32,
                 status->nack_sn, status->offset.start, status->offset.end);
}

static ptrdiff_t encode_last(struct rlc_context *ctx, struct status_pool *pool,
                             void *mem, size_t max_size)
{
        struct rlc_pdu_status *last;
        struct rlc_chunk chunk;
        size_t size;

        last = status_last(pool);
        last->ext.has_more = 1;

        log_rx_status(last);

        size = rlc_status_size(ctx, last);
        if (size > max_size) {
                return -ENOSPC;
        }

        chunk.data = mem;
        chunk.next = NULL;

        rlc_status_encode(ctx, last, &chunk);
        assert(chunk.size == size);

        return size;
}

static ptrdiff_t create_nack_range(struct rlc_context *ctx,
                                   struct status_pool *pool, void *mem,
                                   size_t max_size, struct rlc_sdu *sdu_next,
                                   uint32_t sn)
{
        struct rlc_pdu_status *cur_status;

        rlc_dbgf("Generating NACK range: %" PRIu32 "->%" PRIu32, sn,
                 sdu_next->sn);

        cur_status = status_advance(pool);

        *cur_status = (struct rlc_pdu_status){
                .ext.has_range = 1,
                .nack_sn = sn,
                .range = sdu_next->sn - sn,
        };

        if (status_count(pool) > 1) {
                return encode_last(ctx, pool, mem, max_size);
        }

        return 0;
}

static ptrdiff_t create_nack_offset(struct rlc_context *ctx,
                                    struct status_pool *pool, void *mem_arg,
                                    size_t max_size, struct rlc_sdu *sdu)
{
        struct rlc_pdu_status *cur_status;
        struct rlc_sdu_segment *seg;
        ptrdiff_t bytes;
        size_t remaining;
        uintptr_t mem;

        mem = (uintptr_t)mem_arg;
        remaining = max_size;

        for (rlc_each_node(sdu->segments, seg, next)) {
                /* No more missing segments */
                if (seg->next == NULL && sdu->flags.rx_last_received) {
                        break;
                }

                /* Alternate between two allocated status
                 * structs, so that we can fill the current one
                 * and encode the last one */
                cur_status = status_advance(pool);

                *cur_status = (struct rlc_pdu_status){
                        .ext.has_offset = 1,
                        .nack_sn = sdu->sn,
                };

                if (seg->next != NULL) {
                        /* Between two segments */
                        cur_status->offset.start = seg->seg.end;
                        cur_status->offset.end = seg->next->seg.start;
                } else {
                        /* Last segment registered, but last
                         * segment of the transmission has not
                         * been received */
                        cur_status->offset.start = seg->seg.end;
                        cur_status->offset.end = RLC_STATUS_SO_MAX;
                }

                /* Encode the last status instead of the current
                 * one, so that the E1 bit can be set
                 * appropriately. On the first iteration, skip
                 * encoding as there is no last */
                if (status_count(pool) > 1) {
                        bytes = encode_last(ctx, pool, (void *)mem, remaining);
                        if (bytes == -ENOSPC) {
                                break;
                        }

                        remaining -= (size_t)bytes;
                        mem += (size_t)bytes;
                }
        }

        return max_size - remaining;
}

static void tx_ack(struct rlc_context *ctx, uint16_t sn)
{
        struct rlc_sdu *sdu;
        struct rlc_sdu **lastp;
        uint32_t lowest;

        rlc_dbgf("TX AM STATUS ACK; ACK_SN: %" PRIu32, sn);

        if (rlc_window_base(&ctx->tx.win) >= sn) {
                rlc_wrnf("TX AM STATUS; Dropping as next_ack>=ack_sn (%" PRIu32
                         ">=%" PRIu32 ")",
                         rlc_window_base(&ctx->tx.win), sn);
                return;
        }

        lastp = &ctx->sdus;
        lowest = ctx->tx.next_sn;

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_TX) {
                        if (sdu->sn < sn) {
                                *lastp = sdu->next;

                                rlc_event_tx_done(ctx, sdu);
                                rlc_sdu_dealloc(ctx, sdu);

                                continue;
                        } else if (sdu->sn < lowest) {
                                /* Set to the lowest non-acked SN */
                                lowest = sdu->sn;
                        }
                }

                lastp = &sdu->next;
        }

        rlc_window_move_to(&ctx->tx.win, lowest);
        rlc_dbgf("TX AM: TX_NEXT_ACK=%" PRIu32, lowest);
}

static void tx_win_shift(struct rlc_context *ctx)
{
        struct rlc_sdu *sdu;
        uint32_t lowest;

        lowest = ctx->tx.next_sn;

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_TX && sdu->sn < lowest) {
                        lowest = sdu->sn;
                }
        }

        rlc_window_move_to(&ctx->tx.win, lowest);
        rlc_dbgf("TX AM: TX_NEXT_ACK=%" PRIu32, lowest);
}

static void stop_poll_retransmit(struct rlc_context *ctx)
{
        (void)rlc_timer_stop(ctx->t_poll_retransmit);
}

static void process_nack_offset(struct rlc_context *ctx,
                                struct rlc_pdu_status *cur)
{
        struct rlc_sdu *sdu;
        rlc_errno status;

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_TX && sdu->sn == cur->nack_sn) {
                        break;
                }
        }

        if (sdu == NULL) {
                rlc_errf("Unrecognized SN: %u", cur->nack_sn);
                return;
        }

        if (sdu->sn == ctx->poll_sn) {
                stop_poll_retransmit(ctx);
        }

        if (cur->ext.has_offset) {
                if (cur->offset.end == RLC_STATUS_SO_MAX) {
                        cur->offset.end = sdu->buffer->size;
                }

                status = rlc_sdu_seg_append(ctx, sdu, cur->offset);
                if (status != 0) {
                        rlc_errf("TX AM STATUS; Unable to "
                                 "append seg "
                                 "(%" RLC_PRI_ERRNO ")",
                                 status);
                        return;
                }

                rlc_dbgf("Marking SDU %" PRIu32 " for retransmission (%" PRIu32
                         "->%" PRIu32 ")",
                         sdu->sn, cur->offset.start, cur->offset.end);
                sdu->state = RLC_READY;
        }
}

static void process_nack_range(struct rlc_context *ctx,
                               struct rlc_pdu_status *cur)
{
        struct rlc_sdu *sdu;
        rlc_errno status;
        struct rlc_window nack_win;
        struct rlc_segment seg;

        rlc_window_init(&nack_win, cur->nack_sn, cur->range);

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_TX || sdu->state == RLC_READY) {
                        continue;
                }

                if (rlc_window_has(&nack_win, sdu->sn)) {
                        seg.start = 0;
                        seg.end = sdu->buffer->size;

                        status = rlc_sdu_seg_append(ctx, sdu, seg);
                        if (status != 0) {
                                rlc_errf("TX AM STATUS; Unable "
                                         "to insert nack range "
                                         "segment");
                                return;
                        }

                        sdu->state = RLC_READY;
                }
        }
}

rlc_errno rlc_arq_init(struct rlc_context *ctx)
{
        if (ctx->type == RLC_AM) {
                ctx->t_poll_retransmit =
                        rlc_timer_install(alarm_poll_retransmit, ctx);
                if (!rlc_timer_okay(ctx->t_poll_retransmit)) {
                        return -ENOTSUP;
                }
        }

        return 0;
}

/** @brief Generate and submit status PDU to lower layer */
size_t rlc_arq_tx_status(struct rlc_context *ctx, size_t max_size)
{
        ptrdiff_t ret;
        struct rlc_pdu pdu;
        struct rlc_sdu *sdu;
        struct rlc_chunk chunk;
        struct status_pool pool;
        uintptr_t mem;
        uintptr_t mem_alloc;
        ptrdiff_t bytes;
        uint32_t next_sn;

        (void)memset(&pool, 0, sizeof(pool));
        (void)memset(&pdu, 0, sizeof(pdu));

        pdu.flags.is_status = 1;
        pdu.sn = ctx->rx.highest_ack;

        rlc_dbgf("Generating status report with highest_status=%" PRIu32,
                 ctx->rx.highest_ack);

        next_sn = pdu.sn;

        mem_alloc = (uintptr_t)rlc_alloc(ctx, max_size);
        if (mem_alloc == 0) {
                return -ENOMEM;
        }

        mem = mem_alloc;

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_RX) {
                        continue;
                }

                /* TODO: Generate singular NACK? */
                if (sdu->sn != next_sn) {
                        bytes = create_nack_range(ctx, &pool, (void *)mem,
                                                  max_size, sdu, next_sn);

                        if (bytes == -ENOSPC) {
                                rlc_wrnf("Unable to transmit full status: MTU "
                                         "too low");
                                break;
                        }

                        mem += (size_t)bytes;
                        max_size -= (size_t)bytes;
                }

                if (sdu->state != RLC_DONE) {
                        bytes = create_nack_offset(ctx, &pool, (void *)mem,
                                                   max_size, sdu);
                        if (bytes == -ENOSPC) {
                                rlc_wrnf("Unable to transmit full status: MTU "
                                         "too low");
                                break;
                        }

                        mem += (size_t)bytes;
                        max_size -= (size_t)bytes;
                }

                next_sn = sdu->sn + 1;
        }

        if (status_count(&pool) > 1) {
                encode_last(ctx, &pool, (void *)mem, max_size);

                pdu.flags.ext = 1;
        }

        ctx->gen_status = false;

        rlc_dbgf("Submitting status PDU: SN=%i", pdu.sn);

        chunk.next = NULL;
        chunk.data = (void *)mem_alloc;
        chunk.size = mem - mem_alloc;

        ret = rlc_backend_tx_submit(ctx, &pdu, &chunk, max_size);
        if (ret < 0) {
                rlc_errf("Submitting status failed: %" RLC_PRI_ERRNO,
                         (rlc_errno)ret);

                ret = 0;
        }

        rlc_dealloc(ctx, (void *)mem_alloc);

        return ret;
}

size_t rlc_arq_tx_yield(struct rlc_context *ctx, size_t max_size)
{
        if (ctx->gen_status) {
                return rlc_arq_tx_status(ctx, max_size);
        }

        return 0;
}

/**
 * @brief Determine if the next PDU for @p sdu should poll
 *
 * Returns true if:
 * - Type == AM and
 *   - Retransmitted (not the last segment in the list)
 *   - Bytes without poll exceeded threshhold
 *   - PDU without poll exceeded threshhold
 *   - Last segment
 *
 * @param ctx Context
 * @param sdu
 * @return bool
 * @retval true Poll
 * @retval false Don't poll
 */
bool rlc_arq_tx_pollable(const struct rlc_context *ctx,
                         const struct rlc_sdu *sdu)
{
        return ctx->type == RLC_AM &&
               (ctx->force_poll || sdu->next != NULL ||
                ctx->tx.pdu_without_poll >= ctx->conf->pdu_without_poll_max ||
                ctx->tx.byte_without_poll >= ctx->conf->byte_without_poll_max ||
                (sdu->segments->next == NULL &&
                 sdu->segments->seg.start >= sdu->segments->seg.end));
}

void rlc_arq_tx_register(struct rlc_context *ctx, const struct rlc_pdu *pdu)
{
        if (pdu->flags.polled) {
                ctx->force_poll = false;
        }
}

void rlc_arq_rx_status(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                       const struct rlc_chunk *chunks)
{
        ptrdiff_t bytes;
        size_t offset;
        rlc_errno status;
        struct rlc_pdu_status cur;
        struct rlc_sdu *sdu;

        offset = rlc_pdu_header_size(ctx, pdu);

        tx_ack(ctx, pdu->sn);

        rlc_dbgf("Status PDU received: SN %" PRIu32 ", POLL_SN %" PRIu32,
                 pdu->sn, ctx->poll_sn);

        if (pdu->sn > ctx->poll_sn) {
                stop_poll_retransmit(ctx);
        }

        /* Iterate over every status */
        bytes = rlc_status_decode(ctx, &cur, chunks, offset);
        while (bytes > 0) {
                rlc_dbgf("TX AM STATUS; NACK_SN: %" PRIu32 ", OFFSET: %" PRIu32
                         "->%" PRIu32 ", RANGE: %" PRIu32,
                         cur.nack_sn, cur.offset.start, cur.offset.end,
                         cur.range);

                if (cur.ext.has_range) {
                        process_nack_range(ctx, &cur);
                } else {
                        process_nack_offset(ctx, &cur);
                }

                offset += bytes;
                bytes = rlc_status_decode(ctx, &cur, chunks, offset);
        }

        /* Mark every SDU not known to the RX side for retransmission */
        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_TX || sdu->state == RLC_READY) {
                        continue;
                }

                cur.offset.start = 0;
                cur.offset.end = sdu->buffer->size;

                status = rlc_sdu_seg_append(ctx, sdu, cur.offset);

                if (status != 0) {
                        rlc_errf("TX AM STATUS; Unable to append seg "
                                 "(%" RLC_PRI_ERRNO ")",
                                 status);
                        return;
                }

                sdu->state = RLC_READY;
                sdu->retx_count++;

                if (sdu->sn == ctx->poll_sn) {
                        stop_poll_retransmit(ctx);
                }

                if (sdu->retx_count >= ctx->conf->max_retx_threshhold) {
                        rlc_event_tx_fail(ctx, sdu);
                        rlc_sdu_remove(ctx, sdu);

                        if (sdu->sn == rlc_window_base(&ctx->tx.win)) {
                                tx_win_shift(ctx);
                        }

                        rlc_sdu_dealloc(ctx, sdu);
                }
        }

        if (bytes < 0) {
                rlc_errf("TX AM STATUS; Decode failed: %" RLC_PRI_ERRNO,
                         (rlc_errno)bytes);
                return;
        }
}

void rlc_arq_rx_register(struct rlc_context *ctx, const struct rlc_pdu *pdu)
{
        if (pdu->flags.polled) {
                ctx->gen_status = true;
        }
}
