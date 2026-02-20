
#ifndef RLC_EVENT_H__
#define RLC_EVENT_H__

#include <gabs/pbuf.h>

#include <rlc/utils.h>
#include <rlc/sched.h>

RLC_BEGIN_DECL

struct rlc_event {
        enum rlc_event_type {
                RLC_EVENT_RX_DONE,
                RLC_EVENT_RX_DONE_DIRECT,
                RLC_EVENT_RX_FAIL,

                RLC_EVENT_TX_DONE,
                RLC_EVENT_TX_FAIL
        } type;

        union {
                struct rlc_sdu *sdu;
                gabs_pbuf *buf; /* RX_DONE_DIRECT */
        };

        struct rlc_sched_item sched;
        struct rlc_context *ctx;
};

void rlc_event_rx_done(struct rlc_context *ctx, struct rlc_sdu *sdu);
void rlc_event_rx_done_direct(struct rlc_context *ctx, gabs_pbuf *buf);
void rlc_event_tx_done(struct rlc_context *ctx, struct rlc_sdu *sdu);
void rlc_event_tx_fail(struct rlc_context *ctx, struct rlc_sdu *sdu);
void rlc_event_rx_drop(struct rlc_context *ctx, struct rlc_sdu *sdu);

RLC_END_DECL

#endif /* RLC_EVENT_H__ */
