
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>

#include <rlc/rlc.h>
#include <rlc/timer.h>
#include <rlc/plat.h>
#include <rlc/chunks.h>
#include <rlc/buf.h>

#include "encode.h"
#include "event.h"
#include "sdu.h"
#include "methods.h"

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

static ssize_t do_tx_submit_(struct rlc_context *ctx, struct rlc_pdu *pdu,
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

static ssize_t tx_pdu_view_(struct rlc_context *ctx, struct rlc_pdu *pdu,
                            struct rlc_sdu *sdu, size_t max_size)
{
        size_t num_chunks;
        struct rlc_chunk chunk;

        if (pdu->seg_offset + pdu->size > sdu->buffer->size) {
                return -ENODATA;
        }

        chunk.next = NULL;
        chunk.data = (uint8_t *)sdu->buffer->mem + pdu->seg_offset;
        chunk.size = pdu->size;

        return do_tx_submit_(ctx, pdu, &chunk, max_size);
}

static bool in_window_(uint32_t sn, uint32_t base, uint32_t size)
{
        return base <= sn && sn < base + size;
}

static void prepare_pdu_(const struct rlc_context *ctx, struct rlc_pdu *pdu)
{
        (void)ctx;
        (void)memset(pdu, 0, sizeof(*pdu));
}

/* Section 5.2.3.2.4, "when t-Reassembly expires" */
static bool should_restart_reassembly_(struct rlc_context *ctx)
{
        struct rlc_sdu *sdu;

        if (ctx->rx.next_highest > ctx->rx.highest_status + 1) {
                return true;
        }

        if (ctx->rx.next_highest == ctx->rx.highest_status + 1) {
                sdu = rlc_sdu_get(ctx, ctx->rx.highest_status, RLC_RX);

                if (sdu != NULL && rlc_sdu_loss_detected(sdu)) {
                        return true;
                }
        }

        return false;
}

/* Section 5.2.3.2.3, "if t-Reassembly is not running" */
static bool should_start_reassembly_(struct rlc_context *ctx)
{
        struct rlc_sdu *sdu;
        uint32_t remaining;

        remaining = rlc_window_index(&ctx->rx.win, ctx->rx.next_highest);

        if (remaining > 1) {
                return true;
        }

        if (remaining == 1) {
                sdu = rlc_sdu_get(ctx, rlc_window_base(&ctx->rx.win), RLC_RX);

                if (sdu != NULL && rlc_sdu_loss_detected(sdu)) {
                        return true;
                }
        }

        return false;
}

/* Section 5.2.3.2.3, "if t-Reassembly is running" */
static bool should_stop_reassembly_(struct rlc_context *ctx)
{
        struct rlc_sdu *sdu;
        uint32_t remaining;

        remaining = rlc_window_index(&ctx->rx.win, ctx->rx.next_status_trigger);

        if (remaining == 0) {
                return true;
        }

        if (remaining == 1) {
                sdu = rlc_sdu_get(ctx, rlc_window_base(&ctx->rx.win), RLC_RX);

                if (sdu != NULL && !rlc_sdu_loss_detected(sdu)) {
                        return true;
                }
        }

        if (!rlc_window_has(&ctx->rx.win, ctx->rx.next_status_trigger) &&
            ctx->rx.next_status_trigger != rlc_window_end(&ctx->rx.win)) {
                return true;
        }

        return false;
}

static void alarm_reassembly_(rlc_timer timer, struct rlc_context *ctx)
{
        rlc_errno status;
        struct rlc_sdu *sdu;
        uint32_t lowest;

        rlc_dbgf("Reassembly alarm");

        lowest = ctx->rx.next_highest;

        /* Find the SDU with the lowest SN that is >= RX_Next_status_trigger,
         * and set the highest status to that SN */
        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->sn >= ctx->rx.next_status_trigger &&
                    sdu->sn < lowest) {
                        lowest = sdu->sn;
                }
        }

        ctx->rx.highest_status = lowest;

        rlc_window_move_to(&ctx->rx.win, lowest);

        /* Actions to take for SDUs that are unable to be reassembled does
         * not seem to be specified in the standard. Here, we assume that
         * when highest_status is updated, the next STATUS PDU will send
         * ACK_SN=RX_HIGHEST_STATUS. The transmitting side will then no
         * longer attempt to retransmit parts of the SDUs with SN<ACK_SN,
         * meaning that SDUs with SN<RX_HIGHEST_STATUS will never be
         * received in full. We therefore discard these SDUs when
         * RX_HIGHEST_STATUS is updated. */
        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_RX && sdu->sn < ctx->rx.highest_status) {
                        rlc_event_rx_drop(ctx, sdu);
                        rlc_sdu_remove(ctx, sdu);
                        rlc_sdu_dealloc(ctx, sdu);
                }
        }

        /* If there are any more SDUs which are awaiting more bytes, restart */
        if (should_restart_reassembly_(ctx)) {
                ctx->rx.next_status_trigger = ctx->rx.next_highest;

                rlc_timer_start(timer, ctx->conf->time_reassembly_us);
        }
}

static void alarm_poll_retransmit_(rlc_timer timer, struct rlc_context *ctx)
{
        rlc_errno status;

        rlc_dbgf("Retransmitting poll");

        ctx->force_poll = true;

        (void)rlc_tx_request(ctx);
}

rlc_errno rlc_init(struct rlc_context *ctx, enum rlc_sdu_type type,
                   const struct rlc_config *config,
                   const struct rlc_methods *methods, void *user_data)
{
        (void)memset(ctx, 0, sizeof(*ctx));

        ctx->type = type;
        ctx->methods = methods;
        ctx->conf = config;
        ctx->user_data = user_data;

        rlc_lock_init(&ctx->lock);

        rlc_window_init(&ctx->tx.win, 0, config->window_size);
        rlc_window_init(&ctx->rx.win, 0, config->window_size);

        if (ctx->type == RLC_AM || ctx->type == RLC_UM) {
                ctx->t_reassembly = rlc_timer_install(alarm_reassembly_, ctx);
                if (!rlc_timer_okay(ctx->t_reassembly)) {
                        return -ENOTSUP;
                }

                ctx->t_poll_retransmit =
                        rlc_timer_install(alarm_poll_retransmit_, ctx);
                if (!rlc_timer_okay(ctx->t_poll_retransmit)) {
                        return -ENOTSUP;
                }
        }

        return 0;
}

rlc_errno rlc_send(struct rlc_context *ctx, struct rlc_buf *buf)
{
        rlc_errno status;
        struct rlc_segment seg;
        struct rlc_sdu *sdu;

        if (!rlc_window_has(&ctx->tx.win, ctx->tx.next)) {
                return -ENOSPC;
        }

        sdu = rlc_sdu_alloc(ctx, RLC_TX, buf);
        if (sdu == NULL) {
                return -ENOMEM;
        }

        sdu->sn = ctx->tx.next++;

        rlc_lock_acquire(&ctx->lock);

        rlc_sdu_insert(ctx, sdu);

        seg.start = 0;
        seg.end = sdu->buffer->size;

        rlc_dbgf("TX; Queueing SDU %" PRIu32 ", RANGE: %" PRIu32 "->%" PRIu32,
                 sdu->sn, seg.start, seg.end);

        status = rlc_sdu_seg_append(ctx, sdu, seg);
        if (status != 0) {
                return status;
        }

        rlc_lock_release(&ctx->lock);

        return rlc_tx_request(ctx);
}

static void am_tx_next_ack_update_(struct rlc_context *ctx, uint16_t sn)
{
        struct rlc_sdu *sdu;
        struct rlc_sdu *next;
        struct rlc_sdu **lastp;
        uint32_t lowest;

        rlc_dbgf("TX AM STATUS; ACK_SN: %" PRIu32, sn);

        if (rlc_window_base(&ctx->tx.win) >= sn) {
                rlc_wrnf("TX AM STATUS; Dropping as next_ack>=ack_sn (%" PRIu32
                         ">=%" PRIu32 ")",
                         rlc_window_base(&ctx->tx.win), sn);
                return;
        }

        lastp = &ctx->sdus;
        lowest = ctx->tx.next;

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

static void am_tx_next_ack_increase_(struct rlc_context *ctx)
{
        struct rlc_sdu *sdu;
        uint32_t lowest;

        lowest = ctx->tx.next;

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_TX && sdu->sn < lowest) {
                        lowest = sdu->sn;
                }
        }

        rlc_window_move_to(&ctx->tx.win, lowest);
        rlc_dbgf("TX AM: TX_NEXT_ACK=%" PRIu32, lowest);
}

static void stop_poll_retransmit_(struct rlc_context *ctx)
{
        rlc_dbgf("Stopping t-PollRetransmit");
        (void)rlc_timer_stop(ctx->t_poll_retransmit);
}

static void deliver_acked_(struct rlc_context *ctx, uint32_t ack_sn)
{
        struct rlc_sdu *sdu;
        struct rlc_sdu **lastp;

        lastp = &ctx->sdus;

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_TX && sdu->state != RLC_READY &&
                    sdu->sn < ack_sn) {
                        rlc_dbgf("TX; SDU %" PRIu32
                                 " implied ACK'd, delivering",
                                 sdu->sn);

                        *lastp = sdu->next;

                        rlc_event_tx_done(ctx, sdu);
                        if (sdu->sn == rlc_window_base(&ctx->tx.win)) {
                                am_tx_next_ack_increase_(ctx);
                        }

                        rlc_sdu_dealloc(ctx, sdu);

                        continue;
                }

                lastp = &sdu->next;
        }
}

static void process_nack_chunk_(struct rlc_context *ctx,
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
                stop_poll_retransmit_(ctx);
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

static void process_nack_range_(struct rlc_context *ctx,
                                struct rlc_pdu_status *cur)
{
        struct rlc_sdu *sdu;
        rlc_errno status;

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_TX && sdu->state != RLC_READY &&
                    in_window_(sdu->sn, cur->nack_sn, cur->range)) {
                        status = rlc_sdu_seg_append(
                                ctx, sdu,
                                (struct rlc_segment){
                                        .start = 0,
                                        .end = sdu->buffer->size,
                                });

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

        rlc_dbgf("Status PDU received: SN %" PRIu32 ", POLL_SN %" PRIu32,
                 pdu->sn, ctx->poll_sn);

        if (pdu->sn > ctx->poll_sn) {
                stop_poll_retransmit_(ctx);
        }

        /* Iterate over every status */
        bytes = rlc_status_decode(ctx, &cur, chunks, offset);
        while (bytes > 0) {
                rlc_dbgf("TX AM STATUS; NACK_SN: %" PRIu32 ", OFFSET: %" PRIu32
                         "->%" PRIu32 ", RANGE: %" PRIu32,
                         cur.nack_sn, cur.offset.start, cur.offset.end,
                         cur.range);

                deliver_acked_(ctx, cur.nack_sn);

                if (cur.ext.has_range) {
                        process_nack_range_(ctx, &cur);
                } else {
                        process_nack_chunk_(ctx, &cur);
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
                        stop_poll_retransmit_(ctx);
                }

                if (sdu->retx_count >= ctx->conf->max_retx_threshhold) {
                        rlc_event_tx_fail(ctx, sdu);
                        rlc_sdu_remove(ctx, sdu);

                        if (sdu->sn == rlc_window_base(&ctx->tx.win)) {
                                am_tx_next_ack_increase_(ctx);
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

static uint32_t lowest_sn_not_recv_(struct rlc_context *ctx)
{
        uint32_t lowest;
        struct rlc_sdu *cur;

        lowest = UINT32_MAX;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (cur->dir == RLC_RX && cur->sn < lowest &&
                    !rlc_sdu_is_rx_done(cur)) {
                        lowest = cur->sn;
                }
        }

        return lowest;
}

static void am_rx_highest_status_update_(struct rlc_context *ctx, uint32_t next)
{
        struct rlc_sdu *sdu;

        ctx->rx.highest_status = next;

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_RX) {
                        continue;
                }

                if (sdu->sn < next) {
                        rlc_dbgf("Removing SDU: state %i", sdu->state);
                        assert(sdu->state == RLC_DONE);

                        rlc_sdu_remove(ctx, sdu);
                        rlc_sdu_dealloc(ctx, sdu);
                }
        }
}

void rlc_rx_submit(struct rlc_context *ctx, const struct rlc_chunk *chunks)
{
        ssize_t status;
        size_t header_size;
        uint32_t lowest;
        struct rlc_pdu pdu;
        struct rlc_segment segment;
        struct rlc_sdu *sdu;
        struct rlc_chunk *cur_chunk;

        rlc_lock_acquire(&ctx->lock);

        status = rlc_pdu_decode(ctx, &pdu, chunks);
        if (status != 0) {
                goto exit;
        }

        if (pdu.flags.is_status) {
                process_status_(ctx, &pdu, chunks);

                goto exit;
        }

        if (ctx->type == RLC_AM && pdu.flags.polled) {
                /* Send ACK before receiving any more data */
                ctx->gen_status = true;
        }

        if (ctx->type == RLC_TM) {
                rlc_event_rx_done_direct(ctx, chunks);

                goto exit;
        }

        sdu = rlc_sdu_get(ctx, pdu.sn, RLC_RX);

        if (sdu == NULL) {
                if (!rlc_window_has(&ctx->rx.win, pdu.sn)) {
                        rlc_wrnf("RX; SN %" PRIu32
                                 " outside RX window (%" PRIu32 "->%" PRIu32
                                 "), dropping (highest_status=%" PRIu32 ")",
                                 pdu.sn, rlc_window_base(&ctx->rx.win),
                                 rlc_window_end(&ctx->rx.win),
                                 ctx->rx.highest_status);
                        goto exit;
                }

                sdu = rlc_sdu_alloc(ctx, RLC_RX,
                                    rlc_buf_alloc(ctx, ctx->conf->buffer_size));
                if (sdu == NULL) {
                        rlc_errf("RX; SDU alloc failed (%" RLC_PRI_ERRNO ")",
                                 -ENOMEM);
                        goto exit;
                }

                sdu->state = RLC_READY;
                sdu->sn = pdu.sn;

                rlc_sdu_insert(ctx, sdu);
        }

        if (sdu->state != RLC_READY) {
                rlc_errf("RX; Received SN=%" PRIu32
                         " when not ready, discarding",
                         sdu->sn);
                goto exit;
        }

        if (pdu.seg_offset >= sdu->buffer->size) {
                rlc_errf("RX; Offset out of bounds: %" PRIu32 ">%zu",
                         pdu.seg_offset, sdu->buffer->size);
                goto exit;
        }

        header_size = rlc_pdu_header_size(ctx, &pdu);

        rlc_dbgf("RX; SN: %" PRIu32 ", RANGE: %" PRIu32 "->%zu", pdu.sn,
                 pdu.seg_offset,
                 pdu.seg_offset + rlc_chunks_size(chunks) - header_size);

        /* Copy the contents of the chunks, skipping the header content */
        status = rlc_chunks_deepcopy_view(
                chunks, (uint8_t *)sdu->buffer->mem + pdu.seg_offset,
                sdu->buffer->size - pdu.seg_offset, header_size);
        if (status <= 0) {
                rlc_errf("RX; Unable to flatten chunks: (%" RLC_PRI_ERRNO ")",
                         (rlc_errno)status);
                goto exit;
        }

        segment = (struct rlc_segment){
                .start = pdu.seg_offset,
                .end = pdu.seg_offset + rlc_chunks_size(chunks) - header_size,
        };

        status = rlc_sdu_seg_append(ctx, sdu, segment);
        if (status != 0) {
                rlc_errf("RX; Unable to append segment (%" RLC_PRI_ERRNO ")",
                         (rlc_errno)status);
                goto exit;
        }

        if (pdu.flags.is_last) {
                sdu->flags.rx_last_received = 1;
        }

        if (sdu->sn >= ctx->rx.next_highest) {
                ctx->rx.next_highest = sdu->sn + 1;
        }

        if (rlc_sdu_is_rx_done(sdu)) {
                rlc_inff("RX; SN: %" PRIu32 " completed", sdu->sn);

                rlc_event_rx_done(ctx, sdu);

                /* In acknowledged mode, the SDU should be deallocated when
                 * transmitting the status, so that we can keep the information
                 * in memory until it can be relayed back. The RX buffer can
                 * however be freed to prevent excessive memory use. */
                if (ctx->type == RLC_AM) {
                        lowest = rlc_min(lowest_sn_not_recv_(ctx),
                                         ctx->rx.next_highest);

                        if (sdu->sn == rlc_window_base(&ctx->rx.win)) {
                                rlc_window_move_to(&ctx->rx.win, lowest);
                        }

                        sdu->state = RLC_DONE;

                        if (sdu->sn == ctx->rx.highest_status) {
                                /* After this call, SDU can not be used */
                                am_rx_highest_status_update_(ctx, lowest);
                        }
                } else {
                        rlc_sdu_remove(ctx, sdu);
                        rlc_sdu_dealloc(ctx, sdu);
                }
        }

        if (ctx->type == RLC_AM) {
                if (rlc_timer_active(ctx->t_reassembly) &&
                    should_stop_reassembly_(ctx)) {
                        rlc_dbgf("Stopping t-Reassembly");
                        (void)rlc_timer_stop(ctx->t_reassembly);
                }

                /* This case includes the case of being stopped in the above
                 * case. */
                if (!rlc_timer_active(ctx->t_reassembly) &&
                    should_start_reassembly_(ctx)) {
                        rlc_dbgf("Starting t-Reassembly");

                        ctx->rx.next_status_trigger = ctx->rx.next_highest;
                        (void)rlc_timer_start(ctx->t_reassembly,
                                              ctx->conf->time_reassembly_us);
                }
        }

exit:
        (void)rlc_tx_request(ctx);

        rlc_lock_release(&ctx->lock);
}

/**
 * @brief Adjust the size of @p pdu to fit within @p max_size
 *
 * In UM mode, this may set the is_last flag if the SN can be omitted
 * altogether
 *
 * @param ctx
 * @param pdu
 * @param max_size
 */
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
static bool should_poll_(const struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        return ctx->type == RLC_AM &&
               (ctx->force_poll || sdu->next != NULL ||
                ctx->tx.pdu_without_poll >= ctx->conf->pdu_without_poll_max ||
                ctx->tx.byte_without_poll >= ctx->conf->byte_without_poll_max ||
                (sdu->segments->next == NULL &&
                 sdu->segments->seg.start >= sdu->segments->seg.end));
}

static bool serve_sdu_(struct rlc_context *ctx, struct rlc_sdu *sdu,
                       struct rlc_pdu *pdu, size_t size_avail)
{
        size_t tot_size;
        rlc_errno status;
        struct rlc_sdu_segment *segment;
        struct rlc_sdu *cur;

        segment = sdu->segments;
        rlc_assert(segment != NULL);

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

                        rlc_dealloc(ctx, segment);
                }
        }

        ctx->tx.pdu_without_poll += 1;
        ctx->tx.byte_without_poll += pdu->size;

        pdu->flags.polled = should_poll_(ctx, sdu);
        if (pdu->flags.polled) {
                ctx->tx.pdu_without_poll = 0;
                ctx->tx.byte_without_poll = 0;

                /* Set POLL_SN to the highest SN of the PDUs submitted
                 * to the lower layer */
                for (rlc_each_node(ctx->sdus, cur, next)) {
                        if (cur->dir == RLC_TX &&
                            (cur->segments->seg.start != 0 ||
                             cur->segments->next != NULL) &&
                            cur->sn > ctx->poll_sn) {
                                ctx->poll_sn = cur->sn;
                        }
                }

                ctx->force_poll = false;

                sdu->state = RLC_WAIT;

                status = rlc_timer_restart(ctx->t_poll_retransmit,
                                           ctx->conf->time_poll_retransmit_us);
                if (status == 0) {
                        rlc_dbgf("Started t-PollRetransmit");
                } else {
                        rlc_errf("Unable to start t-PollRetransmit: "
                                 "%" RLC_PRI_ERRNO,
                                 status);
                }

                rlc_dbgf("TX; Polling %" PRIu32 " for status", pdu->sn);
        }

        return true;
}

static struct rlc_chunk *chunk_encode_status_(struct rlc_context *ctx,
                                              struct rlc_pdu_status *status)
{
        struct rlc_chunk *chunk;
        size_t size;
        ssize_t ret;

        size = rlc_status_size(ctx, status);

        chunk = rlc_alloc(ctx, sizeof(*chunk) + size);
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
                rlc_dealloc(ctx, cur);
                cur = next;
        }
}

static void log_rx_status_(struct rlc_pdu_status *status)
{
        rlc_dbgf("RX AM STATUS; Detected missing; SN: "
                 "%" PRIu32 ", RANGE:  %" PRIu32 "->%" PRIu32,
                 status->nack_sn, status->offset.start, status->offset.end);
}

/** @brief Generate and submit status PDU to lower layer */
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
        uint32_t next_sn;

        prepare_pdu_(ctx, &pdu);
        pdu.flags.is_status = 1;
        pdu.sn = ctx->rx.highest_status;

        rlc_dbgf("Generating status report with highest_status=%" PRIu32,
                 ctx->rx.highest_status);

        count = 0;
        status_idx = 0;
        head_chunk = NULL;
        chunkptr = &head_chunk;

        last_status = NULL;
        next_sn = pdu.sn;

        /* TODO: Every state=DONE should generate a nack range */
        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_RX) {
                        continue;
                }

                if (sdu->sn != next_sn) {
                        rlc_dbgf("Generating NACK range: %" PRIu32 "->%" PRIu32,
                                 next_sn, sdu->sn);

                        status_idx = 1 - status_idx;
                        cur_status = &status_pool[status_idx];

                        *cur_status = (struct rlc_pdu_status){
                                .ext.has_range = 1,
                                .nack_sn = next_sn,
                                .range = sdu->sn - next_sn,
                        };

                        /* Encode the last status instead of the current one, so
                         * that the E1 bit can be set appropriately. On the
                         * first iteration, skip encoding as there is no last */
                        /* TODO: deduplicate code */
                        if (last_status != NULL) {
                                last_status->ext.has_more = 1;

                                log_rx_status_(last_status);

                                chunk = chunk_encode_status_(ctx, last_status);
                                if (chunk == NULL) {
                                        ret = -ENOMEM;
                                        goto exit;
                                }

                                *chunkptr = chunk;
                                chunkptr = &chunk->next;
                        }

                        last_status = cur_status;
                        count += 1;
                } else {
                        for (rlc_each_node(sdu->segments, seg, next)) {
                                /* No more missing segments */
                                if (seg->next == NULL &&
                                    sdu->flags.rx_last_received) {
                                        break;
                                }

                                /* Alternate between two allocated status
                                 * structs, so that we can fill the current one
                                 * and encode the last one */
                                status_idx = 1 - status_idx;
                                cur_status = &status_pool[status_idx];

                                *cur_status = (struct rlc_pdu_status){
                                        .ext.has_offset = 1,
                                        .nack_sn = sdu->sn,
                                };

                                if (seg->next != NULL) {
                                        /* Between two segments */
                                        cur_status->offset.start = seg->seg.end;
                                        cur_status->offset.end =
                                                seg->next->seg.start;
                                } else {
                                        /* Last segment registered, but last
                                         * segment of the transmission has not
                                         * been received */
                                        cur_status->offset.start = seg->seg.end;
                                        cur_status->offset.end =
                                                RLC_STATUS_SO_MAX;
                                }

                                /* Encode the last status instead of the current
                                 * one, so that the E1 bit can be set
                                 * appropriately. On the first iteration, skip
                                 * encoding as there is no last */
                                if (last_status != NULL) {
                                        last_status->ext.has_more = 1;

                                        log_rx_status_(last_status);

                                        chunk = chunk_encode_status_(
                                                ctx, last_status);
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

                next_sn = sdu->sn + 1;
        }

        if (count > 0) {
                log_rx_status_(last_status);

                chunk = chunk_encode_status_(ctx, last_status);
                if (chunk == NULL) {
                        ret = -ENOMEM;
                        goto exit;
                }

                *chunkptr = chunk;
                chunkptr = &chunk->next;

                pdu.flags.ext = 1;
        }

        ctx->gen_status = false;

        rlc_dbgf("Submitting status PDU: SN=%i", pdu.sn);

        ret = do_tx_submit_(ctx, &pdu, head_chunk, max_size);
        if (ret < 0) {
                rlc_errf("Submitting status failed: %" RLC_PRI_ERRNO,
                         (rlc_errno)ret);
        }

exit:
        free_chunks_(ctx, head_chunk);

        return ret;
}

static bool should_gen_status_(const struct rlc_context *ctx)
{
        return ctx->gen_status;
}

static struct rlc_sdu *highest_sn_submitted_(struct rlc_context *ctx)
{
        struct rlc_sdu *cur;
        struct rlc_sdu *highest;

        highest = NULL;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (cur->dir != RLC_TX) {
                        continue;
                }

                if (rlc_sdu_submitted(cur) &&
                    (highest == NULL || cur->sn > highest->sn)) {
                        highest = cur;
                }
        }

        return highest;
}

static void force_retransmit_(struct rlc_context *ctx, size_t max_size)
{
        rlc_errno status;
        ssize_t bytes;
        struct rlc_pdu pdu;
        struct rlc_sdu *highest_sn;
        struct rlc_sdu_segment *last;
        struct rlc_sdu_segment *tmp_seg;
        struct rlc_segment seg;
        size_t header_size;

        prepare_pdu_(ctx, &pdu);

        header_size = rlc_pdu_header_size(ctx, &pdu);
        if (header_size > max_size) {
                rlc_errf("Transmit window can not fit minimal header; needs "
                         "%zu, has %zu",
                         header_size, max_size);
                return;
        }

        max_size -= header_size;

        highest_sn = highest_sn_submitted_(ctx);
        if (highest_sn == NULL) {
                rlc_errf("No viable SDU to retransmit poll with");
                return;
        }

        last = NULL;
        for (rlc_each_node(highest_sn->segments, tmp_seg, next)) {
                last = tmp_seg;
        }

        rlc_assert(last != NULL);

        seg.end = last->seg.start;
        seg.start = seg.end - rlc_min(seg.end, max_size);

        status = rlc_sdu_seg_append(ctx, highest_sn, seg);
        if (status != 0) {
                rlc_errf("Unable to forcibly re-append segment to SDU");
                return;
        }

        if (!serve_sdu_(ctx, highest_sn, &pdu, max_size)) {
                rlc_errf("Unable to serve SDU");
                return;
        }

        bytes = tx_pdu_view_(ctx, &pdu, highest_sn, max_size);
        if (bytes < 0) {
                rlc_errf("Unable to transmit PDU view: %" RLC_PRI_ERRNO,
                         (rlc_errno)status);
                return;
        }
}

void rlc_tx_avail(struct rlc_context *ctx, size_t size)
{
        rlc_errno status;
        ssize_t ret;
        size_t tot_size;
        size_t pdu_size;
        const void *data;
        struct rlc_pdu pdu;
        struct rlc_sdu *cur;
        struct rlc_sdu *next;
        struct rlc_sdu *highest_sn;
        struct rlc_sdu_segment *last_seg;
        struct rlc_sdu_segment *tmp_seg;

        rlc_lock_acquire(&ctx->lock);

        rlc_dbgf("TX availability for context %p", ctx);

        if (ctx->type == RLC_AM && should_gen_status_(ctx)) {
                (void)gen_status_(ctx, size);
                rlc_lock_release(&ctx->lock);
                return;
        }

        /* Iterate, without using the `rlc_each_node` macros, for two reasons:
         * 1) We may need to remove the element during iteration, which would
         *    require `rlc_each_node_safe`. However:
         * 2) We release the lock during iteration, meaning that getting the
         *    next element at the start (what is done is `rlc_each_node_safe`)
         *    could cause the next element to be invalid after re-acquiring
         *    the lock.
         * As such, we replicate `rlc_each_node_safe`, but update the `next`
         * pointer after re-acquiring the lock.
         */
        for (cur = ctx->sdus; cur != NULL; cur = next) {
                next = cur->next;

                if (cur->dir != RLC_TX || cur->state != RLC_READY) {
                        continue;
                }

                prepare_pdu_(ctx, &pdu);

                if (!serve_sdu_(ctx, cur, &pdu, size)) {
                        continue;
                }

                rlc_dbgf("TX PDU; SN: %" PRIu32 ", range: %" PRIu32 "->"
                         "%zu",
                         pdu.sn, pdu.seg_offset, pdu.seg_offset + pdu.size);

                /* Release lock while transmitting */
                rlc_lock_release(&ctx->lock);
                ret = tx_pdu_view_(ctx, &pdu, cur, size);
                rlc_lock_acquire(&ctx->lock);

                /* There is a possiblity that the list was modified while the
                 * lock was released, so the asisgnment to next at the start of
                 * the function may now be invalid. */
                next = cur->next;

                if (ret <= 0) {
                        rlc_errf("PDU submit failed: error %" RLC_PRI_ERRNO,
                                 (rlc_errno)ret);
                }

                if (ctx->type != RLC_AM && pdu.flags.is_last) {
                        rlc_event_tx_done(ctx, cur);
                        rlc_sdu_remove(ctx, cur);
                        rlc_dealloc(ctx, cur);
                }

                size -= ret;
                if (size == 0) {
                        break;
                }
        }

        /* Handle expiry of t-PollRetransmit (section 5.3.3.4) */
        if (ctx->type == RLC_AM && ctx->force_poll && size > 0) {
                force_retransmit_(ctx, size);
        }

        rlc_lock_release(&ctx->lock);
}
