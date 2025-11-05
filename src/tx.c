
#include <string.h>

#include <rlc/buf.h>
#include <rlc/sdu.h>
#include <rlc/rlc.h>

#include "encode.h"
#include "event.h"
#include "methods.h"
#include "arq.h"
#include "backend.h"
#include "log.h"

static ptrdiff_t tx_pdu_view(struct rlc_context *ctx, struct rlc_pdu *pdu,
                             struct rlc_sdu *sdu, size_t max_size)
{
        rlc_buf *buf;
        ptrdiff_t ret;

        if (pdu->seg_offset + pdu->size > rlc_buf_size(sdu->buffer)) {
                return -ENODATA;
        }

        rlc_arq_tx_register(ctx, pdu);
        buf = rlc_buf_view(sdu->buffer, pdu->seg_offset, pdu->size, ctx);

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
                rlc_assert(ctx->type == RLC_AM);

                sdu->state = RLC_WAIT;
                return false;
        }

        pdu->seg_offset = segment->seg.start;
        pdu->flags.is_first = pdu->seg_offset == 0;

        pdu_size_adjust(ctx, pdu, size_avail);

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

                        rlc_dealloc(ctx, segment, RLC_ALLOC_SDU_SEGMENT);
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
                        rlc_dbgf("Started t-PollRetransmit");
                } else {
                        rlc_errf("Unable to start t-PollRetransmit: "
                                 "%" RLC_PRI_ERRNO,
                                 status);
                }

                rlc_dbgf("TX; Polling %" PRIu32 " for status", pdu->sn);
        }

        rlc_log_sdu(sdu);

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

                rlc_dbgf("TX PDU; SN: %" PRIu32 ", range: %" PRIu32 "->"
                         "%zu",
                         pdu.sn, pdu.seg_offset, pdu.seg_offset + pdu.size);

                ret = tx_pdu_view(ctx, &pdu, sdu, max_size);
                if (ret <= 0) {
                        rlc_errf("PDU submit failed: error %" RLC_PRI_ERRNO,
                                 (rlc_errno)ret);
                }

                if (ctx->type != RLC_AM && pdu.flags.is_last) {
                        rlc_sem_up(&sdu->tx_sem);
                        rlc_event_tx_done(ctx, sdu);
                        rlc_sdu_remove(ctx, sdu);
                        rlc_dealloc(ctx, sdu, RLC_ALLOC_SDU);
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

        rlc_dbgf("TX availability for context %p", ctx);

        size -= rlc_arq_tx_yield(ctx, size);
        if (size > 0) {
                size -= rlc_tx_yield(ctx, size);
        }

        rlc_lock_release(&ctx->lock);

        return size;
}
