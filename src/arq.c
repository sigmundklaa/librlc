
#include <string.h>
#include <errno.h>

#include <gabs/pbuf.h>

#include <rlc/rlc.h>
#include <rlc/sdu.h>

#include "backend.h"
#include "encode.h"
#include "methods.h"
#include "event.h"
#include "utils.h"
#include "log.h"
#include "tx.h"

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

static void status_advance(struct status_pool *pool)
{
        pool->alloc_count++;
        pool->index = 1 - pool->index;
}

static size_t status_count(struct status_pool *pool)
{
        return pool->alloc_count;
}

static void alarm_poll_retransmit(rlc_timer timer, struct rlc_context *ctx)
{
        gabs_log_dbgf(ctx->logger, "Retransmitting poll");

        ctx->force_poll = true;

        rlc_backend_tx_request(ctx, false);
}

/* Callback is not necessary, we only use the timer for the state */
static void alarm_status_prohibit(rlc_timer timer, struct rlc_context *ctx)
{
        gabs_log_dbgf(ctx->logger, "Status prohibit expired");

        rlc_backend_tx_request(ctx, false);
}

static void log_rx_status(const gabs_logger_h *logger,
                          struct rlc_pdu_status *status)
{
        gabs_log_dbgf(logger,
                      "RX AM STATUS; Detected missing; SN: "
                      "%" PRIu32 ", RANGE:  %" PRIu32 "->%" PRIu32,
                      status->nack_sn, status->offset.start,
                      status->offset.end);
}

static ptrdiff_t encode_last(struct rlc_context *ctx, struct status_pool *pool,
                             gabs_pbuf *buf)
{
        struct rlc_pdu_status *last;
        size_t size;

        last = status_last(pool);
        last->ext.has_more = 1;

        log_rx_status(ctx->logger, last);

        size = rlc_status_size(ctx, last);
        if (size > gabs_pbuf_tailroom(*buf)) {
                return -ENOSPC;
        }

        rlc_status_encode(ctx, last, buf);

        return size;
}

static ptrdiff_t create_nack_range(struct rlc_context *ctx,
                                   struct status_pool *pool, gabs_pbuf *buf,
                                   struct rlc_sdu *sdu_next, uint32_t sn)
{
        struct rlc_pdu_status *cur_status;
        ptrdiff_t ret;
        uint32_t range_diff;

        gabs_log_dbgf(ctx->logger,
                      "Generating NACK range: %" PRIu32 "->%" PRIu32, sn,
                      sdu_next->sn);
        rlc_assert(sdu_next->sn >= sn);

        ret = 0;
        cur_status = status_get(pool);
        range_diff = sdu_next->sn - sn;

        *cur_status = (struct rlc_pdu_status){
                .ext.has_range = range_diff > 1,
                .nack_sn = sn,
                .range = range_diff,
        };

        if (status_count(pool) > 1) {
                ret = encode_last(ctx, pool, buf);
        }

        status_advance(pool);

        return ret;
}

static ptrdiff_t create_nack_offset(struct rlc_context *ctx,
                                    struct status_pool *pool, gabs_pbuf *buf,
                                    struct rlc_sdu *sdu)
{
        struct rlc_pdu_status *cur_status;
        struct rlc_sdu_segment *seg;
        struct rlc_sdu_segment *last;
        ptrdiff_t bytes;
        size_t max_size;
        size_t remaining;

        max_size = gabs_pbuf_tailroom(*buf);
        remaining = max_size;
        last = NULL;

        for (rlc_each_node(sdu->segments, seg, next)) {
                /* No more missing segments */
                if (seg->next == NULL && seg->seg.start == 0 &&
                    sdu->flags.rx_last_received) {
                        break;
                }

                /* Alternate between two allocated status
                 * structs, so that we can fill the current one
                 * and encode the last one */
                cur_status = status_get(pool);

                *cur_status = (struct rlc_pdu_status){
                        .ext.has_offset = 1,
                        .nack_sn = sdu->sn,
                };

                if (last == NULL && seg->seg.start != 0) {
                        cur_status->offset.start = 0;
                        cur_status->offset.end = seg->seg.start;
                } else if (seg->next != NULL) {
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

                gabs_log_dbgf(ctx->logger, "%" PRIu32 "->%" PRIu32,
                              cur_status->offset.start, cur_status->offset.end);

                /* Encode the last status instead of the current
                 * one, so that the E1 bit can be set
                 * appropriately. On the first iteration, skip
                 * encoding as there is no last */
                if (status_count(pool) > 0) {
                        bytes = encode_last(ctx, pool, buf);
                        if (bytes == -ENOSPC) {
                                break;
                        }

                        remaining -= (size_t)bytes;
                }

                status_advance(pool);

                last = seg;
        }

        return max_size - remaining;
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
        gabs_log_dbgf(ctx->logger, "TX AM: TX_NEXT_ACK=%" PRIu32, lowest);
}

static void tx_ack(struct rlc_context *ctx, uint16_t sn)
{
        struct rlc_sdu *sdu;
        struct rlc_sdu **lastp;

        gabs_log_dbgf(ctx->logger, "TX AM STATUS ACK; ACK_SN: %" PRIu32, sn);

        lastp = &ctx->sdus;

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_TX || sdu->sn >= sn ||
                    sdu->state != RLC_WAIT) {
                        lastp = &sdu->next;

                        continue;
                }

                *lastp = sdu->next;

                if (sdu->sn == rlc_window_base(&ctx->tx.win)) {
                        tx_win_shift(ctx);
                }

                rlc_event_tx_done(ctx, sdu);
                rlc_sdu_decref(ctx, sdu);
        }
}

static void tx_nack_clear(struct rlc_context *ctx, uint16_t sn)
{
        struct rlc_sdu *sdu;
        struct rlc_sdu_segment *seg;

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_TX || sdu->sn >= sn) {
                        continue;
                }

                for (rlc_each_node_safe(struct rlc_sdu_segment, sdu->segments,
                                        seg, next)) {
                        if (seg->next == NULL) {
                                sdu->segments = seg;
                                break;
                        }

                        rlc_dealloc(ctx, seg, RLC_ALLOC_SDU_SEGMENT);
                }
        }
}

static void stop_poll_retransmit(struct rlc_context *ctx)
{
        (void)rlc_timer_stop(ctx->t_poll_retransmit);
}

/* Mark for retransmission */
static void retransmit_sdu(struct rlc_context *ctx, struct rlc_sdu *sdu,
                           struct rlc_segment *seg)
{
        struct rlc_segment uniq;
        rlc_errno status;

        status = rlc_sdu_seg_insert(ctx, sdu, seg, &uniq);
        if (status == -ENODATA) {
                /* -ENODATA means there was nothing unique in `seg`, so it won't
                 * be treated as retransmission */
                sdu->state = RLC_READY;

                return;
        } else if (status != 0) {
                gabs_log_errf(ctx->logger,
                              "Unable to insert segment: %" RLC_PRI_ERRNO,
                              status);
                rlc_assert(0);

                return;
        }

        gabs_log_dbgf(ctx->logger,
                      "Marking SDU SN=%" PRIu32 " for (re)transmission",
                      sdu->sn);

        sdu->state = RLC_READY;
        sdu->retx_count++;

        if (sdu->retx_count >= ctx->conf->max_retx_threshhold) {
                gabs_log_errf(ctx->logger,
                              "Transmit failed; exceeded retry limit");

                rlc_event_tx_fail(ctx, sdu);
                rlc_sdu_remove(ctx, sdu);
                rlc_sdu_decref(ctx, sdu);
        }
}

static void process_nack_offset(struct rlc_context *ctx,
                                struct rlc_pdu_status *cur)
{
        struct rlc_sdu *sdu;

        sdu = rlc_sdu_get(ctx, cur->nack_sn, RLC_TX);
        if (sdu == NULL) {
                gabs_log_errf(ctx->logger, "Unrecognized SN: %u", cur->nack_sn);

                return;
        }

        if (sdu->sn == ctx->poll_sn) {
                stop_poll_retransmit(ctx);
        }

        if (cur->ext.has_offset) {
                if (cur->offset.end == RLC_STATUS_SO_MAX) {
                        cur->offset.end = gabs_pbuf_size(sdu->buffer);
                }

                retransmit_sdu(ctx, sdu, &cur->offset);
        }
}

static void process_nack(struct rlc_context *ctx, struct rlc_pdu_status *cur)
{
        struct rlc_sdu *sdu;
        struct rlc_segment seg;

        sdu = rlc_sdu_get(ctx, cur->nack_sn, RLC_TX);
        if (sdu == NULL) {
                gabs_log_errf(ctx->logger, "Unknown SDU: %" PRIu32,
                              cur->nack_sn);

                return;
        }

        seg.start = 0;
        seg.end = gabs_pbuf_size(sdu->buffer);

        retransmit_sdu(ctx, sdu, &cur->offset);
}

static void process_nack_range(struct rlc_context *ctx,
                               struct rlc_pdu_status *cur)
{
        struct rlc_sdu *sdu;
        struct rlc_window nack_win;
        struct rlc_segment seg;

        rlc_window_init(&nack_win, cur->nack_sn, cur->range);

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_TX || sdu->state == RLC_READY) {
                        continue;
                }

                if (rlc_window_has(&nack_win, sdu->sn)) {
                        seg.start = 0;
                        seg.end = gabs_pbuf_size(sdu->buffer);

                        /* NOTE: May remove SDU */
                        retransmit_sdu(ctx, sdu, &seg);
                }
        }
}

/**
 * @brief Generate and submit status PDU to lower layer
 *
 * @param ctx
 * @param max_size Maximum available bytes in transmit window.
 *
 * @return Number of bytes transmitted
 */
static size_t tx_status(struct rlc_context *ctx, size_t max_size)
{
        ptrdiff_t ret;
        rlc_errno status;
        struct rlc_pdu pdu;
        struct rlc_sdu *sdu;
        struct status_pool pool;
        gabs_pbuf buf;
        ptrdiff_t bytes;
        uint32_t next_sn;

        (void)memset(&pool, 0, sizeof(pool));
        (void)memset(&pdu, 0, sizeof(pdu));

        next_sn = rlc_window_base(&ctx->rx.win);

        buf = gabs_pbuf_new(ctx->alloc_buf, max_size);
        if (!gabs_pbuf_okay(buf)) {
                return -ENOMEM;
        }

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_RX) {
                        continue;
                }

                if (sdu->sn != next_sn) {
                        bytes = create_nack_range(ctx, &pool, &buf, sdu,
                                                  next_sn);
                        if (bytes == -ENOSPC) {
                                gabs_log_wrnf(
                                        ctx->logger,
                                        "Unable to transmit full status: MTU "
                                        "too low");
                                break;
                        }

                        max_size -= (size_t)bytes;
                }

                if (sdu->state != RLC_DONE) {
                        bytes = create_nack_offset(ctx, &pool, &buf, sdu);
                        if (bytes == -ENOSPC) {
                                gabs_log_wrnf(
                                        ctx->logger,
                                        "Unable to transmit full status: MTU "
                                        "too low");
                                break;
                        }

                        max_size -= (size_t)bytes;
                }

                next_sn = sdu->sn + 1;
        }

        if (status_count(&pool) > 0) {
                encode_last(ctx, &pool, &buf);

                pdu.flags.ext = 1;
        }

        /* The RLC spec states: "set the ACK_SN to the SN of the next not
         * received RLC SDU which is not indicated as missing in the resulting
         * STATUS PDU". This is assumed to mean the SN of the SDU after the ones
         * we have included in the STATUS PDU. */
        pdu.sn = next_sn;
        pdu.flags.is_status = 1;

        ctx->gen_status = false;

        status = rlc_timer_start(ctx->t_status_prohibit,
                                 ctx->conf->time_status_prohibit_us);
        if (status != 0) {
                gabs_log_errf(
                        ctx->logger,
                        "Unable to start t-statusProhibit: %" RLC_PRI_ERRNO,
                        status);

                rlc_assert(0);
        }

        gabs_log_dbgf(ctx->logger, "Submitting status PDU: SN=%i", pdu.sn);

        ret = rlc_backend_tx_submit(ctx, &pdu, buf);
        if (ret < 0) {
                gabs_log_errf(ctx->logger,
                              "Submitting status failed: %" RLC_PRI_ERRNO,
                              (rlc_errno)ret);

                ret = 0;
        }

        return ret;
}

static struct rlc_sdu *highest_sn_submitted(struct rlc_context *ctx)
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

static struct rlc_sdu_segment *last_segment(struct rlc_sdu *sdu)
{
        struct rlc_sdu_segment *last;
        struct rlc_sdu_segment *cur;

        last = NULL;

        for (rlc_each_node(sdu->segments, cur, next)) {
                last = cur;
        }

        return last;
}

static size_t tx_poll(struct rlc_context *ctx, size_t max_size)
{
        size_t ret;
        size_t header_size;
        struct rlc_sdu *sdu;
        struct rlc_sdu_segment *last_seg;
        struct rlc_segment seg;
        struct rlc_pdu pdu;

        /* First try and transmit the poll with an unsubmitted segment */
        ret = rlc_tx_yield(ctx, max_size);

        /* This should have been cleared if anything has been transmitted. If
         * not, we need to retransmit something to include the poll */
        if (ctx->force_poll) {
                (void)memset(&pdu, 0, sizeof(pdu));
                header_size = rlc_pdu_header_size(ctx, &pdu);
                if (header_size > max_size) {
                        /* TODO: issue tx request? */
                        gabs_log_errf(
                                ctx->logger,
                                "Transmit window can not fit minimal header; "
                                "needs %zu, has %zu",
                                header_size, max_size);
                        return ret;
                }

                sdu = highest_sn_submitted(ctx);
                if (sdu == NULL) {
                        gabs_log_wrnf(
                                ctx->logger,
                                "Unable to get SDU to retransmit poll with");
                        return ret;
                }

                last_seg = last_segment(sdu);
                if (last_seg == NULL) {
                        rlc_assert(0);
                        return ret;
                }

                seg.end = last_seg->seg.end;
                seg.start = seg.end - rlc_min(seg.end, max_size - header_size);

                retransmit_sdu(ctx, sdu, &seg);
        }

        return ret;
}

size_t rlc_arq_tx_yield(struct rlc_context *ctx, size_t max_size)
{
        size_t ret;

        ret = 0;

        if (ctx->gen_status && !rlc_timer_active(ctx->t_status_prohibit)) {
                ret += tx_status(ctx, max_size);
        }

        if (ctx->force_poll) {
                ret += tx_poll(ctx, max_size);
        }

        return ret;
}

bool rlc_arq_tx_pollable(const struct rlc_context *ctx,
                         const struct rlc_sdu *sdu)
{
        const struct rlc_sdu *cur;

        if (ctx->type != RLC_AM) {
                return false;
        }

        if (ctx->force_poll) {
                return true;
        }

        if (ctx->tx.pdu_without_poll >= ctx->conf->pdu_without_poll_max ||
            ctx->tx.byte_without_poll >= ctx->conf->byte_without_poll_max) {
                return true;
        }

        /* Check if tranmission buffer is empty */
        for (rlc_each_node(sdu, cur, next)) {
                if (cur->dir != RLC_TX || sdu->segments->next != NULL) {
                        continue;
                }

                if (sdu->segments->seg.start < sdu->segments->seg.end) {
                        return false;
                }
        }

        /* No more buffers to transmit - include poll */
        return true;
}

void rlc_arq_tx_register(struct rlc_context *ctx, const struct rlc_pdu *pdu)
{
        if (pdu->flags.polled) {
                ctx->force_poll = false;
        }
}

void rlc_arq_rx_status(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                       gabs_pbuf *buf)
{
        size_t offset;
        rlc_errno status;
        struct rlc_pdu_status cur;

        offset = rlc_pdu_header_size(ctx, pdu);

        gabs_log_wrnf(ctx->logger,
                      "Status PDU received: SN %" PRIu32 ", POLL_SN %" PRIu32
                      ", %zu",
                      pdu->sn, ctx->poll_sn, gabs_pbuf_size(*buf));

        if (pdu->sn > ctx->poll_sn) {
                stop_poll_retransmit(ctx);
        }

        tx_nack_clear(ctx, pdu->sn);

        /* Iterate over every status */
        while ((status = rlc_status_decode(ctx, &cur, buf)) == 0) {
                gabs_log_dbgf(ctx->logger,
                              "TX AM STATUS; NACK_SN: %" PRIu32
                              ", OFFSET: %" PRIu32 "->%" PRIu32
                              ", RANGE: %" PRIu32,
                              cur.nack_sn, cur.offset.start, cur.offset.end,
                              cur.range);

                if (cur.ext.has_range) {
                        process_nack_range(ctx, &cur);
                } else if (cur.ext.has_offset) {
                        process_nack_offset(ctx, &cur);
                } else {
                        process_nack(ctx, &cur);
                }
        }

        tx_ack(ctx, pdu->sn);
}

void rlc_arq_rx_register(struct rlc_context *ctx, const struct rlc_pdu *pdu)
{
        if (pdu->flags.polled) {
                ctx->gen_status = true;
        }
}

rlc_errno rlc_arq_init(struct rlc_context *ctx)
{
        if (ctx->type == RLC_AM) {
                ctx->t_poll_retransmit =
                        rlc_timer_install(alarm_poll_retransmit, ctx, 0);
                if (!rlc_timer_okay(ctx->t_poll_retransmit)) {
                        return -ENOTSUP;
                }

                ctx->t_status_prohibit =
                        rlc_timer_install(alarm_status_prohibit, ctx, 0);
                if (!rlc_timer_okay(ctx->t_status_prohibit)) {
                        return -ENOTSUP;
                }
        }

        return 0;
}

rlc_errno rlc_arq_deinit(struct rlc_context *ctx)
{
        rlc_errno status;

        status = rlc_timer_uninstall(ctx->t_status_prohibit);
        if (status != 0) {
                return status;
        }

        status = rlc_timer_uninstall(ctx->t_poll_retransmit);
        return status;
}
