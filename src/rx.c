
#include <errno.h>

#include <rlc/rlc.h>
#include <rlc/chunks.h>
#include <rlc/buf.h>

#include "arq.h"
#include "methods.h"
#include "event.h"
#include "encode.h"
#include "sdu.h"

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
                return true;
        }

        return false;
}

static void alarm_reassembly(rlc_timer timer, struct rlc_context *ctx)
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

        ctx->rx.highest_ack = lowest;

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
                if (sdu->dir == RLC_RX && sdu->sn < ctx->rx.highest_ack) {
                        rlc_event_rx_drop(ctx, sdu);
                        rlc_sdu_remove(ctx, sdu);
                        rlc_sdu_dealloc(ctx, sdu);
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
                        assert(sdu->state == RLC_DONE);

                        rlc_sdu_remove(ctx, sdu);
                        rlc_sdu_dealloc(ctx, sdu);
                }
        }
}

rlc_errno rlc_rx_init(struct rlc_context *ctx)
{
        if (ctx->type != RLC_TM) {
                ctx->t_reassembly = rlc_timer_install(alarm_reassembly, ctx);
                if (!rlc_timer_okay(ctx->t_reassembly)) {
                        return -ENOTSUP;
                }
        }

        return 0;
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

        if (ctx->type == RLC_TM) {
                rlc_event_rx_done_direct(ctx, chunks);

                goto exit;
        }

        if (pdu.flags.is_status) {
                rlc_arq_rx_status(ctx, &pdu, chunks);

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
                        rlc_sdu_dealloc(ctx, sdu);
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
        (void)rlc_tx_request(ctx);

        rlc_lock_release(&ctx->lock);
}
