
#ifndef RLC_EVENT_H__
#define RLC_EVENT_H__

#include <rlc/rlc.h>

#include "utils.h"

RLC_BEGIN_DECL

void rlc_event_fire(struct rlc_context *ctx, struct rlc_event *event);
void rlc_event_rx_done(struct rlc_context *ctx, struct rlc_sdu *sdu);
void rlc_event_rx_done_direct(struct rlc_context *ctx, rlc_buf *buf);
void rlc_event_tx_done(struct rlc_context *ctx, struct rlc_sdu *sdu);
void rlc_event_tx_fail(struct rlc_context *ctx, struct rlc_sdu *sdu);
void rlc_event_rx_drop(struct rlc_context *ctx, struct rlc_sdu *sdu);

RLC_END_DECL

#endif /* RLC_EVENT_H__ */
