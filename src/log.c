
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <rlc/sdu.h>
#include <rlc/utils.h>
#include <rlc/rlc.h>

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

static const char *fmt_segments(const struct rlc_sdu *sdu, char *buf,
                                size_t max_size, const char *prefix)
{
        int bytes;
        int ret;
        struct rlc_sdu_segment *seg;

        bytes = 0;

        for (rlc_each_node(sdu->segments, seg, next)) {
                ret = snprintf(buf + bytes, max_size - bytes,
                               "%s%" PRIu32 "->%" PRIu32 "\n", prefix,
                               seg->seg.start, seg->seg.end);
                if (ret < 0) {
                        rlc_assert(0);
                        break;
                }

                bytes += ret;
        }

        return buf;
}

static void log_tx_sdu(const gabs_logger_h *logger, const struct rlc_sdu *sdu)
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
                      sdu->retx_count,
                      fmt_segments(sdu, buf, sizeof(buf), "\t\t"));
}

static void log_rx_sdu(const gabs_logger_h *logger, const struct rlc_sdu *sdu)
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
                      sdu->flags.rx_last_received,
                      fmt_segments(sdu, buf, sizeof(buf), "\t\t"));
}

static void log_window(struct rlc_context *ctx, enum rlc_sdu_dir dir,
                       struct rlc_window *win)
{
        struct rlc_sdu *cur;

        gabs_log_dbgf(ctx->logger, "%s window(%" PRIu32 "->%" PRIu32 "): {",
                      dir == RLC_TX ? "TX" : "RX", rlc_window_base(win),
                      rlc_window_end(win));

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (cur->dir == dir) {
                        rlc_log_sdu(ctx->logger, cur);
                }
        }

        gabs_log_dbgf(ctx->logger, "}");
}

void rlc_log_tx_window(struct rlc_context *ctx)
{
        log_window(ctx, RLC_TX, &ctx->tx.win);
}

void rlc_log_rx_window(struct rlc_context *ctx)
{
        log_window(ctx, RLC_RX, &ctx->rx.win);
}

void rlc_log_sdu(const gabs_logger_h *logger, const struct rlc_sdu *sdu)
{
        if (sdu->dir == RLC_TX) {
                log_tx_sdu(logger, sdu);
        } else {
                log_rx_sdu(logger, sdu);
        }
}
