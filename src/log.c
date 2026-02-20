
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <rlc/sdu.h>
#include <rlc/utils.h>
#include <rlc/rlc.h>
#include <rlc/seg_list.h>

#include "log.h"

static const char *sdu_state_str(enum rlc_sdu_state state)
{
        switch (state) {
        case RLC_READY:
                return "RLC_READY";
        case RLC_DONE:
                return "RLC_DONE";
        case RLC_WAIT:
                return "RLC_WAIT";
        }

        rlc_assert(0);
}

static const char *fmt_segments(rlc_seg_list *list, char *buf, size_t max_size,
                                const char *prefix)
{
        int bytes;
        int ret;
        struct rlc_seg_item *seg_item;
        rlc_list_it it;

        bytes = 0;

        rlc_list_foreach(list, it)
        {
                seg_item = rlc_seg_item_from_it(it);
                ret = snprintf(buf + bytes, max_size - bytes,
                               "%s%" PRIu32 "->%" PRIu32 "\n", prefix,
                               seg_item->seg.start, seg_item->seg.end);
                if (ret < 0) {
                        rlc_assert(0);
                        break;
                }

                bytes += ret;
        }

        return buf;
}

void rlc_log_tx_sdu(const gabs_logger_h *logger, struct rlc_sdu *sdu)
{
        char buf[128];

        (void)memset(buf, 0, sizeof(buf));

        gabs_log_dbgf(logger,
                      "SDU %" PRIu32 ": {\n\t"
                      "state: %s\n\t"
                      "rc: %u\n\t"
                      "retx_count: %u\n\t"
                      "segments: {\n"
                      "%s"
                      "\t}\n"
                      "}",
                      sdu->sn, sdu_state_str(sdu->state), sdu->refcount,
                      sdu->tx.retx_count,
                      fmt_segments(&sdu->tx.unsent, buf, sizeof(buf), "\t\t"));
}

void rlc_log_rx_sdu(const gabs_logger_h *logger, struct rlc_sdu *sdu)
{
        char buf[128];

        (void)memset(buf, 0, sizeof(buf));

        gabs_log_dbgf(logger,
                      "SDU %" PRIu32 ": {\n\t"
                      "state: %s\n\t"
                      "rc: %u\n\t"
                      "last_received: %d\n\t"
                      "segments: {\n"
                      "%s"
                      "\t}\n"
                      "}",
                      sdu->sn, sdu_state_str(sdu->state), sdu->refcount,
                      sdu->rx.last_received,
                      fmt_segments(&sdu->rx.buffer.segments, buf, sizeof(buf), "\t\t"));
}

static void log_window(struct rlc_context *ctx, rlc_sdu_queue *q,
                       struct rlc_window *win, bool rx)
{
        struct rlc_sdu *cur;
        rlc_list_it it;

        gabs_log_dbgf(ctx->logger, "%s window(%" PRIu32 "->%" PRIu32 "): {",
                      rx ? "RX" : "TX", rlc_window_base(win),
                      rlc_window_end(win));

        rlc_list_foreach(q, it)
        {
                cur = rlc_sdu_from_it(it);

                if (rx) {
                        rlc_log_rx_sdu(ctx->logger, cur);
                } else {
                        rlc_log_tx_sdu(ctx->logger, cur);
                }
        }

        gabs_log_dbgf(ctx->logger, "}");
}

void rlc_log_tx_window(struct rlc_context *ctx)
{
        log_window(ctx, &ctx->tx.sdus, &ctx->tx.win, false);
}

void rlc_log_rx_window(struct rlc_context *ctx)
{
        log_window(ctx, &ctx->rx.sdus, &ctx->rx.win, true);
}
