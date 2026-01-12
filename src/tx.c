
#include <string.h>

#include <rlc/sdu.h>
#include <rlc/rlc.h>

#include "encode.h"
#include "methods.h"
#include "arq.h"
#include "backend.h"
#include "common.h"
#include "log.h"

static ptrdiff_t tx_pdu_view(struct rlc_context *ctx, struct rlc_pdu *pdu,
                             struct rlc_sdu *sdu, size_t max_size)
{
        gabs_pbuf buf;
        ptrdiff_t ret;

        if (pdu->seg_offset + pdu->size > gabs_pbuf_size(sdu->buffer)) {
                return -ENODATA;
        }

        rlc_arq_tx_register(ctx, pdu);
        buf = gabs_pbuf_view(sdu->buffer, pdu->seg_offset, pdu->size,
                             ctx->alloc_buf);

        ret = rlc_backend_tx_submit(ctx, pdu, buf);

        return ret;
}

/*
 * @brief Adjust the size of @p pdu to fit within @p max_size
 *
 * In UM mode, this may set the is_last flag if the SN can be omitted
 * altogether
 *
 * @param ctx
 * @param pdu
 * @param max_size
 */
static void pdu_size_adjust(const struct rlc_context *ctx, struct rlc_pdu *pdu,
                            size_t max_size)
{
        size_t hsize;

        if (ctx->conf->type == RLC_UM && pdu->flags.is_first) {
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

static bool serve_sdu(struct rlc_context *ctx, struct rlc_sdu *sdu,
                      struct rlc_pdu *pdu, size_t size_avail)
{
        rlc_errno status;
        struct rlc_sdu_segment *segment;
        struct rlc_sdu *cur;

        segment = sdu->segments;
        rlc_assert(segment != NULL);

        pdu->sn = sdu->sn;
        pdu->size = segment->seg.end - segment->seg.start;
        if (pdu->size == 0) {
                rlc_assert(ctx->conf->type == RLC_AM);

                sdu->state = RLC_WAIT;
                return false;
        }

        pdu->seg_offset = segment->seg.start;
        pdu->flags.is_first = pdu->seg_offset == 0;

        pdu_size_adjust(ctx, pdu, size_avail);

        /* Only the last segment should be updated. Any previous segment
         * is added for retransmission, and should only be updated when
         * receiving ACK for it. */
        if (segment->next == NULL) {
                segment->seg.start += pdu->size;

                if (segment->seg.start >= segment->seg.end) {
                        /* If last segment, set last flag and go into waiting
                         * state. The last segment is kept alive until the
                         * SDU is deallocated, so that it can be used to
                         * distuingish between retransmitted PDUs and
                         * first-time-transmitted PDUs. */
                        sdu->state = RLC_WAIT;
                        pdu->flags.is_last = 1;
                }
        }

        ctx->tx.pdu_without_poll += 1;
        ctx->tx.byte_without_poll += pdu->size;

        pdu->flags.polled = rlc_arq_tx_pollable(ctx, sdu);
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

                sdu->state = RLC_WAIT;

                status = rlc_timer_restart(ctx->t_poll_retransmit,
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
        }

        rlc_log_sdu(ctx->logger, sdu);

        return true;
}

size_t rlc_tx_yield(struct rlc_context *ctx, size_t max_size)
{
        struct rlc_sdu *sdu;
        struct rlc_pdu pdu;
        ptrdiff_t ret;
        size_t size;

        size = 0;

        /* Iterate, without using the `rlc_each_node` macros, for two reasons:
         * 1) We may need to remove the element during iteration, which would
         *    require `rlc_each_node_safe`. However:
         * 2) We release the lock during iteration, meaning that getting the
         *    next element at the start (what is done is `rlc_each_node_safe`)
         *    could cause the next element to be invalid after re-acquiring
         *    the lock.
         */
        for (;;) {
                /* This is a bit inefficient, but allows for modification of
                 * the list if tx_pdu_view re-enters from the user backend.
                 * In that case, the current SDU could potentially be
                 * removed, meaning we need to restart iteration to ensure
                 * the correct elements are used.
                 */
                for (rlc_each_node(ctx->sdus, sdu, next)) {
                        if (sdu->dir == RLC_TX && sdu->state == RLC_READY) {
                                break;
                        }
                }

                if (sdu == NULL) {
                        break;
                }

                (void)memset(&pdu, 0, sizeof(pdu));

                if (!serve_sdu(ctx, sdu, &pdu, max_size)) {
                        continue;
                }

                gabs_log_dbgf(ctx->logger,
                              "TX PDU; SN: %" PRIu32 ", range: %" PRIu32 "->"
                              "%zu",
                              pdu.sn, pdu.seg_offset,
                              pdu.seg_offset + pdu.size);

                ret = tx_pdu_view(ctx, &pdu, sdu, max_size);
                if (ret <= 0) {
                        gabs_log_errf(
                                ctx->logger,
                                "PDU submit failed: error %" RLC_PRI_ERRNO,
                                (rlc_errno)ret);
                }

                if (ctx->conf->type != RLC_AM && pdu.flags.is_last) {
                        rlc_event_tx_done(ctx, sdu);
                        rlc_sdu_remove(ctx, sdu);
                        rlc_dealloc(ctx, sdu);
                }

                size += (size_t)ret;

                max_size -= ret;
                if (max_size == 0) {
                        break;
                }
        }

        return size;
}

size_t rlc_tx_avail(struct rlc_context *ctx, size_t size)
{
        rlc_lock_acquire(&ctx->lock);

        gabs_log_dbgf(ctx->logger, "TX availability for context %p", ctx);

        size -= rlc_arq_tx_yield(ctx, size);
        if (size > 0) {
                size -= rlc_tx_yield(ctx, size);
        }

        rlc_lock_release(&ctx->lock);

        rlc_sched_yield(&ctx->sched);

        return size;
}

rlc_errno rlc_tx(struct rlc_context *ctx, gabs_pbuf buf,
                 struct rlc_sdu **sdu_out)
{
        struct rlc_segment seg;
        struct rlc_sdu *sdu;
        rlc_errno status;

        if (!rlc_window_has(&ctx->tx.win, ctx->tx.next_sn)) {
                return -ENOSPC;
        }

        sdu = rlc_sdu_alloc(ctx, RLC_TX);
        if (sdu == NULL) {
                return -ENOMEM;
        }

        gabs_pbuf_incref(buf);

        sdu->sn = ctx->tx.next_sn++;
        sdu->buffer = buf;

        rlc_lock_acquire(&ctx->lock);

        seg.start = 0;
        seg.end = gabs_pbuf_size(sdu->buffer);

        gabs_log_dbgf(ctx->logger,
                      "TX; Queueing SDU %" PRIu32 ", RANGE: %" PRIu32
                      "->%" PRIu32,
                      sdu->sn, seg.start, seg.end);

        status = rlc_sdu_seg_insert_all(ctx, sdu, seg);
        if (status != 0) {
                rlc_lock_release(&ctx->lock);
                rlc_sdu_decref(ctx, sdu);
                gabs_pbuf_decref(buf);

                return status;
        }

        rlc_sdu_insert(ctx, sdu);

        if (sdu_out != NULL) {
                *sdu_out = sdu;

                rlc_sdu_incref(sdu);
        }

        rlc_lock_release(&ctx->lock);

        rlc_sched_yield(&ctx->sched);
        rlc_backend_tx_request(ctx, false);

        return 0;
}
