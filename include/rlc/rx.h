
#ifndef RLC_RX_H__
#define RLC_RX_H__

#include <gabs/pbuf.h>

#include <rlc/errno.h>

RLC_BEGIN_DECL

struct rlc_context;

rlc_errno rlc_rx_init(struct rlc_context *ctx);
rlc_errno rlc_rx_deinit(struct rlc_context *ctx);

void rlc_rx_submit(struct rlc_context *ctx, gabs_pbuf buf);

RLC_END_DECL

#endif /* RLC_RX_H__ */
