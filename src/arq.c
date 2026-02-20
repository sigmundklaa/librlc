
#include <string.h>
#include <errno.h>

#include <gabs/pbuf.h>

#include <rlc/rlc.h>
#include <rlc/sdu.h>
#include <rlc/backend.h>

#include "encode.h"
#include "log.h"

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

        ctx->arq.force_poll = true;

        rlc_backend_tx_request(ctx);
}

static void alarm_status_prohibit(rlc_timer timer, struct rlc_context *ctx)
{
        gabs_log_dbgf(ctx->logger, "Status prohibit expired");

        ctx->arq.status_prohibit = false;

        rlc_backend_tx_request(ctx);
}

static rlc_errno restart_status_prohibit(struct rlc_context *ctx)
{
        rlc_errno status;

        status = rlc_timer_restart(ctx->arq.t_status_prohibit,
                                   ctx->conf->time_status_prohibit_us);
        if (status == 0) {
                ctx->arq.status_prohibit = true;
        }

        return status;
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
        struct rlc_seg_item *seg;
        struct rlc_seg_item *last;
        struct rlc_seg_item *next;
        bool has_more;
        ptrdiff_t bytes;
        size_t max_size;
        size_t remaining;
        rlc_list_it it;

        max_size = gabs_pbuf_tailroom(*buf);
        remaining = max_size;
        last = NULL;

        rlc_list_foreach(&sdu->rx.buffer.segments, it)
        {
                seg = rlc_seg_item_from_it(it);
                next = rlc_seg_item_from_it(rlc_list_it_next(it));
                has_more = !rlc_list_it_eoi(rlc_list_it_next(it));

                /* No more missing segments */
                if (!has_more && seg->seg.start == 0 && sdu->rx.last_received) {
                        /* TODO: This should be safe to remove, assuming this
                         * function is not called for done SDUs? */
                        rlc_assert(0);
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
                } else if (has_more) {
                        /* Between two segments */
                        cur_status->offset.start = seg->seg.end;
                        cur_status->offset.end = next->seg.start;
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

        sdu = rlc_sdu_queue_head(&ctx->tx.sdus);
        lowest = sdu == NULL ? ctx->tx.next_sn : sdu->sn;

        rlc_window_move_to(&ctx->tx.win, lowest);
        gabs_log_dbgf(ctx->logger, "TX AM: TX_NEXT_ACK=%" PRIu32, lowest);
}

static void tx_ack(struct rlc_context *ctx, uint16_t sn)
{
        struct rlc_sdu *sdu;
        rlc_list_it it;

        gabs_log_dbgf(ctx->logger, "TX AM STATUS ACK; ACK_SN: %" PRIu32, sn);

        rlc_list_foreach(&ctx->tx.sdus, it)
        {
                sdu = rlc_sdu_from_it(it);

                if (sdu->state == RLC_READY) {
                        continue;
                }

                if (sdu->sn >= sn) {
                        break;
                }

                it = rlc_list_it_pop(it, NULL);

                if (sdu->sn == rlc_window_base(&ctx->tx.win)) {
                        tx_win_shift(ctx);
                }

                rlc_event_tx_done(ctx, sdu);
                rlc_sdu_decref(sdu);
        }
}

static void tx_nack_clear(struct rlc_context *ctx, uint16_t sn)
{
        struct rlc_sdu *sdu;
        rlc_list_it it;

        rlc_list_foreach(&ctx->tx.sdus, it)
        {
                sdu = rlc_sdu_from_it(it);

                if (sdu->sn >= sn) {
                        break;
                }

                rlc_seg_list_clear_until_last(&sdu->tx.unsent, ctx->alloc_misc);
        }
}

static void stop_poll_retransmit(struct rlc_context *ctx)
{
        (void)rlc_timer_stop(ctx->arq.t_poll_retransmit);
}

/**
 * @brief Mark for retransmission
 *
 * @return bool
 * @retval false SDU is removed and possibly invalid after this call
 * @retval true SDU is still in the queue and valid
 */
static bool retransmit_sdu(struct rlc_context *ctx, struct rlc_sdu *sdu,
                           struct rlc_seg *seg)
{
        struct rlc_seg uniq;
        rlc_errno status;

        status = rlc_seg_list_insert(&sdu->tx.unsent, seg, &uniq,
                                     ctx->alloc_misc);
        if (status == -ENODATA) {
                /* -ENODATA means there was nothing unique in `seg`, so it won't
                 * be treated as retransmission */
                sdu->state = RLC_READY;

                return true;
        } else if (status != 0) {
                gabs_log_errf(ctx->logger,
                              "Unable to insert segment: %" RLC_PRI_ERRNO,
                              status);
                rlc_assert(0);

                return true;
        }

        gabs_log_dbgf(ctx->logger,
                      "Marking SDU SN=%" PRIu32 " for (re)transmission",
                      sdu->sn);

        sdu->state = RLC_READY;
        sdu->tx.retx_count++;

        if (sdu->tx.retx_count >= ctx->conf->max_retx_threshhold) {
                gabs_log_errf(ctx->logger,
                              "Transmit failed; exceeded retry limit");
                rlc_sdu_queue_remove(&ctx->tx.sdus, sdu);

                if (sdu->sn == rlc_window_base(&ctx->tx.win)) {
                        tx_win_shift(ctx);
                }

                rlc_event_tx_fail(ctx, sdu);
                rlc_sdu_decref(sdu);

                return false;
        }

        return true;
}

static void process_nack_offset(struct rlc_context *ctx,
                                struct rlc_pdu_status *cur)
{
        struct rlc_sdu *sdu;

        sdu = rlc_sdu_queue_get(&ctx->tx.sdus, cur->nack_sn);
        if (sdu == NULL) {
                gabs_log_errf(ctx->logger, "Unrecognized SN: %u", cur->nack_sn);

                return;
        }

        if (sdu->sn == ctx->arq.poll_sn) {
                stop_poll_retransmit(ctx);
        }

        if (cur->ext.has_offset) {
                if (cur->offset.end == RLC_STATUS_SO_MAX) {
                        cur->offset.end = gabs_pbuf_size(sdu->tx.buffer);
                }

                (void)retransmit_sdu(ctx, sdu, &cur->offset);
        }
}

static void process_nack(struct rlc_context *ctx, struct rlc_pdu_status *cur)
{
        struct rlc_sdu *sdu;
        struct rlc_seg seg;

        sdu = rlc_sdu_queue_get(&ctx->tx.sdus, cur->nack_sn);
        if (sdu == NULL) {
                gabs_log_errf(ctx->logger, "Unknown SDU: %" PRIu32,
                              cur->nack_sn);

                return;
        }

        seg.start = 0;
        seg.end = gabs_pbuf_size(sdu->tx.buffer);

        (void)retransmit_sdu(ctx, sdu, &seg);
}

static void process_nack_range(struct rlc_context *ctx,
                               struct rlc_pdu_status *cur)
{
        struct rlc_sdu *sdu;
        struct rlc_window nack_win;
        struct rlc_seg seg;
        rlc_list_it it;
        rlc_list_it skipped_to;

        rlc_window_init(&nack_win, cur->nack_sn, cur->range);

        rlc_list_foreach(&ctx->tx.sdus, it)
        {
                sdu = rlc_sdu_from_it(it);

                if (sdu->state == RLC_READY) {
                        continue;
                }

                if (rlc_window_has(&nack_win, sdu->sn)) {
                        seg.start = 0;
                        seg.end = gabs_pbuf_size(sdu->tx.buffer);
                        skipped_to = rlc_list_it_skip(it);

                        /* Adjust iterator if the SDU is removed/deallocated so
                         * that we don't need to read from invalid memory when
                         * we get the next item in the iterator. */
                        if (!retransmit_sdu(ctx, sdu, &seg)) {
                                it = rlc_list_it_repeat(skipped_to);
                        }
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
        rlc_list_it it;
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

        rlc_list_foreach(&ctx->rx.sdus, it)
        {
                sdu = rlc_sdu_from_it(it);

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

        ctx->arq.gen_status = false;

        status = restart_status_prohibit(ctx);
        if (status != 0) {
                gabs_log_errf(
                        ctx->logger,
                        "Unable to restart t-statusProhibit: %" RLC_PRI_ERRNO,
                        status);

                rlc_assert(0);
        } else {
                gabs_log_dbgf(ctx->logger, "Started t-statusProhibit");
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
        rlc_list_it it;

        highest = NULL;

        rlc_list_foreach(&ctx->tx.sdus, it)
        {
                cur = rlc_sdu_from_it(it);

                if (rlc_sdu_submitted(cur) &&
                    (highest == NULL || cur->sn > highest->sn)) {
                        highest = cur;
                }
        }

        return highest;
}

static struct rlc_seg_item *last_segment(rlc_seg_list *list)
{
        rlc_list_it it;
        rlc_list_it last;

        rlc_list_foreach(list, it)
        {
                last = it;
        }

        return rlc_seg_item_from_it(last);
}

static size_t tx_poll(struct rlc_context *ctx, size_t max_size)
{
        size_t ret;
        size_t header_size;
        struct rlc_sdu *sdu;
        struct rlc_seg_item *last_seg;
        struct rlc_seg seg;
        struct rlc_pdu pdu;

        /* First try and transmit the poll with an unsubmitted segment */
        ret = rlc_tx_yield(ctx, max_size);

        /* This should have been cleared if anything has been transmitted. If
         * not, we need to retransmit something to include the poll */
        if (ctx->arq.force_poll) {
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

                last_seg = last_segment(&sdu->tx.unsent);
                if (last_seg == NULL) {
                        rlc_assert(0);
                        return ret;
                }

                seg.end = last_seg->seg.end;
                seg.start = seg.end - rlc_min(seg.end - seg.start,
                                              max_size - header_size);

                (void)retransmit_sdu(ctx, sdu, &seg);
        }

        return ret;
}

size_t rlc_arq_tx_yield(struct rlc_context *ctx, size_t max_size)
{
        size_t ret;

        ret = 0;

        if (ctx->arq.gen_status && !ctx->arq.status_prohibit) {
                ret += tx_status(ctx, max_size);
        }

        if (ctx->arq.force_poll) {
                ret += tx_poll(ctx, max_size);
        }

        return ret;
}

static bool tx_pollable(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        rlc_list_it it;
        struct rlc_seg_item *seg_item;

        if (ctx->conf->type != RLC_AM) {
                return false;
        }

        if (ctx->arq.force_poll) {
                return true;
        }

        if (ctx->arq.pdu_without_poll >= ctx->conf->pdu_without_poll_max ||
            ctx->arq.byte_without_poll >= ctx->conf->byte_without_poll_max) {
                return true;
        }

        it = rlc_list_it_init(&sdu->tx.unsent);
        seg_item = rlc_seg_item_from_it(it);

        if (!rlc_list_it_eoi(rlc_list_it_next(it))) {
                return false;
        }

        if (seg_item->seg.start < seg_item->seg.end) {
                return false;
        }

        /* No more buffers to transmit - include poll */
        return true;
}

static void adjust_poll_sn(struct rlc_context *ctx)
{
        rlc_list_it it;
        struct rlc_sdu *sdu;

        rlc_list_foreach(&ctx->tx.sdus, it)
        {
                sdu = rlc_sdu_from_it(it);

                /* Set POLL_SN to the highest SN of the PDUs submitted
                 * to the lower layer */
                if (rlc_sdu_submitted(sdu) && sdu->sn > ctx->arq.poll_sn) {
                        ctx->arq.poll_sn = sdu->sn;
                }
        }
}

void rlc_arq_tx_pdu_fill(struct rlc_context *ctx, struct rlc_sdu *sdu,
                         struct rlc_pdu *pdu)
{
        rlc_errno status;

        ctx->arq.pdu_without_poll += 1;
        ctx->arq.byte_without_poll += pdu->size;

        pdu->flags.polled = tx_pollable(ctx, sdu);
        if (pdu->flags.polled) {
                ctx->arq.pdu_without_poll = 0;
                ctx->arq.byte_without_poll = 0;

                adjust_poll_sn(ctx);

                sdu->state = RLC_WAIT;

                status = rlc_timer_restart(ctx->arq.t_poll_retransmit,
                                           ctx->conf->time_poll_retransmit_us);
                if (status == 0) {
                        gabs_log_dbgf(ctx->logger, "Started t-PollRetransmit");
                } else {
                        gabs_log_errf(ctx->logger,
                                      "Unable to start t-PollRetransmit: "
                                      "%" RLC_PRI_ERRNO,
                                      status);
                }

                gabs_log_dbgf(ctx->logger, "TX; Polling %" PRIu32 " for status",
                              pdu->sn);

                ctx->arq.force_poll = false;
        }
}

void rlc_arq_rx_status(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                       gabs_pbuf *buf)
{
        size_t offset;
        rlc_errno status;
        struct rlc_pdu_status cur;

        offset = rlc_pdu_header_size(ctx, pdu);

        gabs_log_dbgf(ctx->logger,
                      "Status PDU received: SN %" PRIu32 ", POLL_SN %" PRIu32
                      ", %zu",
                      pdu->sn, ctx->arq.poll_sn, gabs_pbuf_size(*buf));

        if (pdu->sn > ctx->arq.poll_sn) {
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
                ctx->arq.gen_status = true;
        }
}

rlc_errno rlc_arq_init(struct rlc_context *ctx)
{
        if (ctx->conf->type == RLC_AM) {
                ctx->arq.t_poll_retransmit =
                        rlc_timer_install(alarm_poll_retransmit, ctx, 0);
                if (!rlc_timer_okay(ctx->arq.t_poll_retransmit)) {
                        return -ENOTSUP;
                }

                ctx->arq.t_status_prohibit =
                        rlc_timer_install(alarm_status_prohibit, ctx, 0);
                if (!rlc_timer_okay(ctx->arq.t_status_prohibit)) {
                        return -ENOTSUP;
                }
        }

        return 0;
}

rlc_errno rlc_arq_deinit(struct rlc_context *ctx)
{
        rlc_errno status;

        status = rlc_timer_uninstall(ctx->arq.t_status_prohibit);
        if (status != 0) {
                return status;
        }

        status = rlc_timer_uninstall(ctx->arq.t_poll_retransmit);
        return status;
}
