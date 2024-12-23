
#include <errno.h>

#include <rlc/rlc.h>
#include <rlc/buf.h>
#include <rlc/sdu.h>

#include "arq.h"
#include "backend.h"
#include "event.h"
#include "encode.h"
#include "log.h"

/* Section 5.2.3.2.4, "when t-Reassembly expires" */
static bool should_restart_reassembly(struct rlc_context *ctx)
{
        struct rlc_sdu *sdu;

        if (ctx->rx.next_highest > ctx->rx.highest_ack + 1) {
                return true;
        }

        if (ctx->rx.next_highest == ctx->rx.highest_ack + 1) {
                sdu = rlc_sdu_get(ctx, ctx->rx.highest_ack, RLC_RX);

                if (sdu != NULL && rlc_sdu_loss_detected(sdu)) {
                        return true;
                }
        }

        return false;
}

/* Section 5.2.3.2.3, "if t-Reassembly is not running" */
static bool should_start_reassembly(struct rlc_context *ctx)
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
static bool should_stop_reassembly(struct rlc_context *ctx)
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
                return false;
        }

        return false;
}

static void alarm_reassembly(rlc_timer timer, struct rlc_context *ctx)
{
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

        ctx->rx.highest_ack = lowest;

        /* When failing reception, the context is pretty much corrupted as
         * SDUs need to be received in order. So, this currently does not
         * advance the `rx.next`, potentially making the context stuck at this
         * SDU. This, however, needs to be handled outside the RLC layer. */
        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_RX && sdu->sn < ctx->rx.highest_ack) {
                        rlc_event_rx_drop(ctx, sdu);
                        rlc_sdu_remove(ctx, sdu);
                        rlc_sdu_decref(ctx, sdu);
                }
        }

        /* If there are any more SDUs which are awaiting more bytes, restart */
        if (should_restart_reassembly(ctx)) {
                ctx->rx.next_status_trigger = ctx->rx.next_highest;

                rlc_timer_start(timer, ctx->conf->time_reassembly_us);
        }
}

static uint32_t lowest_sn_not_recv(struct rlc_context *ctx)
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

static void highest_ack_update(struct rlc_context *ctx, uint32_t next)
{
        struct rlc_sdu *sdu;

        ctx->rx.highest_ack = next;

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_RX) {
                        continue;
                }

                if (sdu->sn < next) {
                        rlc_dbgf("Removing SDU: state %i", sdu->state);
                        rlc_assert(sdu->state == RLC_DONE);

                        rlc_sdu_remove(ctx, sdu);
                        rlc_sdu_decref(ctx, sdu);
                }
        }
}

static void log_rx_state(struct rlc_sdu *sdu)
{
        struct rlc_sdu_segment *seg;

        rlc_dbgf("RX Status of SDU SN=%" PRIu32 ":", sdu->sn);

        for (rlc_each_node(sdu->segments, seg, next)) {
                rlc_dbgf("\t%" PRIu32 "->%" PRIu32, seg->seg.start,
                         seg->seg.end);
        }
}

rlc_errno rlc_rx_init(struct rlc_context *ctx)
{
        if (ctx->type != RLC_TM) {
                ctx->t_reassembly = rlc_timer_install(alarm_reassembly, ctx, 0);
                if (!rlc_timer_okay(ctx->t_reassembly)) {
                        return -ENOTSUP;
                }
        }

        return 0;
}

rlc_errno rlc_rx_deinit(struct rlc_context *ctx)
{
        return rlc_timer_uninstall(ctx->t_reassembly);
}

static rlc_errno insert_buffer(struct rlc_context *ctx, struct rlc_sdu *sdu,
                               rlc_buf *buf, struct rlc_segment seg)
{
        struct rlc_segment unique;
        struct rlc_segment cur;
        rlc_buf *insertbuf;
        size_t offset;
        rlc_errno status;

        status = 0;

        do {
                cur = seg;

                status = rlc_sdu_seg_insert(ctx, sdu, &seg, &unique);
                if (status != 0) {
                        if (status == -ENODATA) {
                                status = 0;
                        }

                        break;
                }

                /* No remaining parts of the buffer that need to be inserted, so
                 * we don't need to create a new buffer. This is the most likely
                 * case. */
                if (!rlc_segment_okay(&seg)) {
                        rlc_buf_incref(buf);
                        insertbuf = buf;

                        if (unique.start != cur.start) {
                                insertbuf = rlc_buf_strip_front(
                                        insertbuf, unique.start - cur.start,
                                        ctx);
                        }

                        if (unique.end != cur.end) {
                                insertbuf = rlc_buf_strip_back(
                                        insertbuf, cur.end - unique.end, ctx);
                        }
                } else {
                        offset = unique.start - cur.start;
                        insertbuf = rlc_buf_clone(
                                buf, offset,
                                offset + (unique.end - unique.start), ctx);

                        /* Strip off the bytes that are now handled by the new
                         * buffer, in addition to the bytes that are already
                         * inserted (which is the difference between the start
                         * of the remaining and the end of the unique). */
                        buf = rlc_buf_strip_front(
                                buf,
                                offset + rlc_buf_size(insertbuf) +
                                        (seg.start - unique.end),
                                ctx);
                }

                sdu->buffer = rlc_buf_chain_at(
                        sdu->buffer, insertbuf,
                        rlc_sdu_seg_byte_offset(sdu, unique.start));
        } while (rlc_segment_okay(&seg));

        return status;
}

rlc_buf *rlc_rx_submit(struct rlc_context *ctx, rlc_buf *buf)
{
        ptrdiff_t status;
        uint32_t lowest;
        struct rlc_pdu pdu;
        struct rlc_segment segment;
        struct rlc_sdu *sdu;

        rlc_lock_acquire(&ctx->lock);

        status = rlc_pdu_decode(ctx, &pdu, &buf);
        if (status != 0) {
                rlc_errf("Decode failed: %" RLC_PRI_ERRNO, (rlc_errno)status);
                goto exit;
        }

        if (ctx->type == RLC_TM) {
                rlc_event_rx_done_direct(ctx, buf);

                goto exit;
        }

        if (pdu.flags.is_status) {
                buf = rlc_arq_rx_status(ctx, &pdu, buf);

                goto exit;
        }

        if (ctx->type == RLC_AM && pdu.flags.polled) {
                rlc_arq_rx_register(ctx, &pdu);
        }

        sdu = rlc_sdu_get(ctx, pdu.sn, RLC_RX);

        if (sdu == NULL) {
                if (!rlc_window_has(&ctx->rx.win, pdu.sn)) {
                        rlc_wrnf("RX; SN %" PRIu32
                                 " outside RX window (%" PRIu32 "->%" PRIu32
                                 "), dropping (highest_status=%" PRIu32 ")",
                                 pdu.sn, rlc_window_base(&ctx->rx.win),
                                 rlc_window_end(&ctx->rx.win),
                                 ctx->rx.highest_ack);
                        goto exit;
                }

                sdu = rlc_sdu_alloc(ctx, RLC_RX);
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
                rlc_wrnf("RX; Received SN=%" PRIu32
                         " when not ready, discarding",
                         sdu->sn);
                goto exit;
        }

        rlc_dbgf("RX; SN: %" PRIu32 ", RANGE: %" PRIu32 "->%zu", pdu.sn,
                 pdu.seg_offset, pdu.seg_offset + rlc_buf_size(buf));

        segment = (struct rlc_segment){
                .start = pdu.seg_offset,
                .end = pdu.seg_offset + rlc_buf_size(buf),
        };

        status = insert_buffer(ctx, sdu, buf, segment);
        if (status != 0) {
                rlc_errf("Buffer insertion failed: %" RLC_PRI_ERRNO,
                         (rlc_errno)status);
                goto exit;
        }

        if (pdu.flags.is_last) {
                sdu->flags.rx_last_received = 1;
        }

        if (sdu->sn >= ctx->rx.next_highest) {
                ctx->rx.next_highest = sdu->sn + 1;
        }

        log_rx_state(sdu);

        if (rlc_sdu_is_rx_done(sdu)) {
                rlc_inff("RX; SN: %" PRIu32 " completed", sdu->sn);

                rlc_event_rx_done(ctx, sdu);

                /* In acknowledged mode, we must wait until after receiving
                 * the status before deallocating. */
                if (ctx->type == RLC_AM) {
                        lowest = rlc_min(lowest_sn_not_recv(ctx),
                                         ctx->rx.next_highest);

                        if (sdu->sn == rlc_window_base(&ctx->rx.win)) {
                                rlc_window_move_to(&ctx->rx.win, lowest);
                        }

                        sdu->state = RLC_DONE;

                        if (sdu->sn == ctx->rx.highest_ack) {
                                /* After this call, SDU can not be used */
                                highest_ack_update(ctx, lowest);
                        }
                } else {
                        rlc_sdu_remove(ctx, sdu);
                        rlc_sdu_decref(ctx, sdu);
                }
        }

        if (ctx->type == RLC_AM || ctx->type == RLC_UM) {
                if (rlc_timer_active(ctx->t_reassembly) &&
                    should_stop_reassembly(ctx)) {
                        rlc_dbgf("Stopping t-Reassembly");
                        (void)rlc_timer_stop(ctx->t_reassembly);
                }

                /* This case includes the case of being stopped in the above
                 * case. */
                if (!rlc_timer_active(ctx->t_reassembly) &&
                    should_start_reassembly(ctx)) {
                        rlc_dbgf("Starting t-Reassembly");

                        ctx->rx.next_status_trigger = ctx->rx.next_highest;
                        (void)rlc_timer_start(ctx->t_reassembly,
                                              ctx->conf->time_reassembly_us);
                }
        }
exit:
        rlc_backend_tx_request(ctx, true);

        rlc_lock_release(&ctx->lock);

        return buf;
}
