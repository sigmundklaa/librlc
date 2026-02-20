
#ifndef RLC_EVENT_H__
#define RLC_EVENT_H__

#include <gabs/pbuf.h>

#include <rlc/utils.h>
#include <rlc/sched.h>

RLC_BEGIN_DECL

struct rlc_context;

/**
 * @brief Event notified to user code.
 *
 * The valid event types are:
 * - `RLC_EVENT_RX_DONE` - RX complete, deliver SDU
 * - `RLC_EVENT_RX_DONE_DIRECt` - RX complete, deliver buffer directly (no SDU)
 * - `RLC_EVENT_RX_FAIL` - Reception failed. SDU is being dropped
 * - `RLC_EVENT_TX_RELEASE` - TX SDU is either completed or dropped.
 */
struct rlc_event {
        enum rlc_event_type {
                RLC_EVENT_RX_DONE,
                RLC_EVENT_RX_DONE_DIRECT,
                RLC_EVENT_RX_FAIL,

                RLC_EVENT_TX_RELEASE,
        } type;

        union {
                struct {
                        struct rlc_sdu *sdu;
                } rx_done;

                struct {
                        gabs_pbuf *buf;
                } rx_done_direct;

                struct {
                        struct rlc_sdu *sdu;
                } rx_fail;

                struct {
                        struct rlc_sdu *sdu;
                } tx_release;

                struct rlc_sdu *sdu;
                gabs_pbuf *buf;
        };

        struct rlc_sched_item sched;
        struct rlc_context *ctx;
};

typedef void (*rlc_event_listener)(struct rlc_context *,
                                   const struct rlc_event *event);

void rlc_event_rx_done(struct rlc_context *ctx, struct rlc_sdu *sdu);
void rlc_event_rx_done_direct(struct rlc_context *ctx, gabs_pbuf *buf);
void rlc_event_tx_done(struct rlc_context *ctx, struct rlc_sdu *sdu);
void rlc_event_tx_fail(struct rlc_context *ctx, struct rlc_sdu *sdu);
void rlc_event_rx_drop(struct rlc_context *ctx, struct rlc_sdu *sdu);

RLC_END_DECL

#endif /* RLC_EVENT_H__ */
