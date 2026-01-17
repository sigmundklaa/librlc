
#include <errno.h>
#include <string.h>

#include <rlc/rlc.h>
#include <rlc/sdu.h>

#include "arq.h"
#include "backend.h"
#include "encode.h"
#include "log.h"
#include "common.h"

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

static void deliver_sdu(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        gabs_log_inff(ctx->logger, "Delivering SDU %i", sdu->sn);

        rlc_event_rx_done(ctx, sdu);
        rlc_sdu_remove(ctx, sdu);
        rlc_sdu_decref(ctx, sdu);
}

static void drop_sdu(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        gabs_log_wrnf(ctx->logger, "Dropping SDU %i", sdu->sn);

        rlc_event_rx_drop(ctx, sdu);
        rlc_sdu_remove(ctx, sdu);
        rlc_sdu_decref(ctx, sdu);
}

static void alarm_reassembly(rlc_timer timer, struct rlc_context *ctx)
{
        struct rlc_sdu *sdu;
        uint32_t lowest;
        uint32_t next;

        gabs_log_dbgf(ctx->logger, "Reassembly alarm");

        lowest = ctx->rx.next_highest;
        next = rlc_window_base(&ctx->rx.win);

        /* Find the SDU with the lowest SN that is >= RX_Next_status_trigger,
         * and set the highest status to that SN */
        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_RX) {
                        continue;
                }

                if (next >= ctx->rx.next_status_trigger &&
                    sdu->state != RLC_DONE) {
                        lowest = next;
                        break;
                }

                next += 1;
        }

        ctx->rx.highest_ack = lowest;
        rlc_window_move_to(&ctx->rx.win, lowest);

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir == RLC_RX && sdu->sn < lowest) {
                        if (sdu->state == RLC_DONE) {
                                deliver_sdu(ctx, sdu);
                        } else {
                                drop_sdu(ctx, sdu);
                        }
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

static void deliver_ready(struct rlc_context *ctx)
{
        struct rlc_sdu *sdu;

        for (rlc_each_node_safe(struct rlc_sdu, ctx->sdus, sdu, next)) {
                if (sdu->dir != RLC_RX) {
                        continue;
                }

                if (sdu->state != RLC_DONE) {
                        break;
                }

                deliver_sdu(ctx, sdu);
        }
}

rlc_errno rlc_rx_init(struct rlc_context *ctx)
{
        if (ctx->conf->type != RLC_TM) {
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
                               gabs_pbuf *buf, struct rlc_segment seg)
{
        struct rlc_segment unique;
        struct rlc_segment cur;
        gabs_pbuf insertbuf;
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
                        gabs_pbuf_incref(*buf);

                        if (unique.start != cur.start) {
                                gabs_pbuf_strip_head(buf,
                                                     unique.start - cur.start);
                        }

                        if (unique.end != cur.end) {
                                gabs_pbuf_strip_tail(buf, cur.end - unique.end);
                        }

                        insertbuf = *buf;
                } else {
                        offset = unique.start - cur.start;
                        insertbuf = gabs_pbuf_clone(
                                *buf, offset,
                                offset + (unique.end - unique.start),
                                ctx->alloc_buf);

                        /* Strip off the bytes that are now handled by the new
                         * buffer, in addition to the bytes that are already
                         * inserted (which is the difference between the start
                         * of the remaining and the end of the unique). */
                        gabs_pbuf_strip_head(
                                buf, offset + gabs_pbuf_size(insertbuf) +
                                             (seg.start - unique.end));
                }

                gabs_pbuf_chain_at(&sdu->buffer, insertbuf,
                                   rlc_sdu_seg_byte_offset(sdu, unique.start));
        } while (rlc_segment_okay(&seg));

        return status;
}

void rlc_rx_submit(struct rlc_context *ctx, gabs_pbuf buf)
{
        ptrdiff_t status;
        uint32_t lowest;
        struct rlc_pdu pdu;
        struct rlc_segment segment;
        struct rlc_sdu *sdu;

        rlc_lock_acquire(&ctx->lock);

        status = rlc_pdu_decode(ctx, &pdu, &buf);
        if (status != 0) {
                gabs_log_errf(ctx->logger, "Decode failed: %" RLC_PRI_ERRNO,
                              (rlc_errno)status);
                goto exit;
        }

        if (ctx->conf->type == RLC_TM) {
                rlc_event_rx_done_direct(ctx, &buf);

                goto exit;
        }

        if (pdu.flags.is_status) {
                rlc_arq_rx_status(ctx, &pdu, &buf);

                goto exit;
        }

        if (ctx->conf->type == RLC_AM && pdu.flags.polled) {
                rlc_arq_rx_register(ctx, &pdu);
        }

        sdu = rlc_sdu_get(ctx, pdu.sn, RLC_RX);

        if (sdu == NULL) {
                if (!rlc_window_has(&ctx->rx.win, pdu.sn)) {
                        gabs_log_wrnf(
                                ctx->logger,
                                "RX; SN %" PRIu32 " outside RX window (%" PRIu32
                                "->%" PRIu32
                                "), dropping (highest_status=%" PRIu32 ")",
                                pdu.sn, rlc_window_base(&ctx->rx.win),
                                rlc_window_end(&ctx->rx.win),
                                ctx->rx.highest_ack);
                        goto exit;
                }

                sdu = rlc_sdu_alloc(ctx, RLC_RX);
                if (sdu == NULL) {
                        gabs_log_errf(ctx->logger,
                                      "RX; SDU alloc failed (%" RLC_PRI_ERRNO
                                      ")",
                                      -ENOMEM);
                        goto exit;
                }

                sdu->state = RLC_READY;
                sdu->sn = pdu.sn;

                rlc_sdu_insert(ctx, sdu);
        }

        if (sdu->state != RLC_READY) {
                gabs_log_wrnf(ctx->logger,
                              "RX; Received SN=%" PRIu32
                              " when not ready, discarding",
                              sdu->sn);
                goto exit;
        }

        gabs_log_dbgf(ctx->logger,
                      "RX; SN: %" PRIu32 ", RANGE: %" PRIu32 "->%zu", pdu.sn,
                      pdu.seg_offset, pdu.seg_offset + gabs_pbuf_size(buf));

        segment = (struct rlc_segment){
                .start = pdu.seg_offset,
                .end = pdu.seg_offset + gabs_pbuf_size(buf),
        };

        status = insert_buffer(ctx, sdu, &buf, segment);
        if (status != 0) {
                gabs_log_errf(ctx->logger,
                              "Buffer insertion failed: %" RLC_PRI_ERRNO,
                              (rlc_errno)status);
                goto exit;
        }

        if (pdu.flags.is_last) {
                sdu->flags.rx_last_received = 1;
        }

        if (sdu->sn >= ctx->rx.next_highest) {
                ctx->rx.next_highest = sdu->sn + 1;
        }

        rlc_log_sdu(ctx->logger, sdu);
        rlc_log_rx_window(ctx);

        if (rlc_sdu_is_rx_done(sdu)) {
                gabs_log_inff(ctx->logger, "RX; SN: %" PRIu32 " completed",
                              sdu->sn);

                /* In acknowledged mode, we must wait until after receiving
                 * the status before deallocating. */
                if (ctx->conf->type == RLC_AM) {
                        lowest = rlc_min(lowest_sn_not_recv(ctx),
                                         ctx->rx.next_highest);

                        gabs_log_dbgf(ctx->logger,
                                      "Shifting RX window to %" PRIu32, lowest);

                        if (sdu->sn == rlc_window_base(&ctx->rx.win)) {
                                rlc_window_move_to(&ctx->rx.win, lowest);
                        }

                        sdu->state = RLC_DONE;

                        if (sdu->sn == ctx->rx.highest_ack) {
                                ctx->rx.highest_ack = lowest;
                        }

                        deliver_ready(ctx);
                } else {
                        rlc_sdu_remove(ctx, sdu);
                        rlc_sdu_decref(ctx, sdu);
                }
        }

        if (ctx->conf->type == RLC_AM || ctx->conf->type == RLC_UM) {
                if (rlc_timer_active(ctx->t_reassembly) &&
                    should_stop_reassembly(ctx)) {
                        gabs_log_dbgf(ctx->logger, "Stopping t-Reassembly");
                        (void)rlc_timer_stop(ctx->t_reassembly);
                }

                /* This case includes the case of being stopped in the above
                 * case. */
                if (!rlc_timer_active(ctx->t_reassembly) &&
                    should_start_reassembly(ctx)) {
                        gabs_log_dbgf(ctx->logger, "Starting t-Reassembly");

                        ctx->rx.next_status_trigger = ctx->rx.next_highest;
                        (void)rlc_timer_start(ctx->t_reassembly,
                                              ctx->conf->time_reassembly_us);
                }
        }
exit:
        rlc_backend_tx_request(ctx);
        gabs_pbuf_decref(buf);

        rlc_lock_release(&ctx->lock);
        rlc_sched_yield(&ctx->sched);
}
