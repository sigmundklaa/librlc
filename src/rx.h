
#ifndef RLC_RX_H__
#define RLC_RX_H__

#include <rlc/rlc.h>

RLC_BEGIN_DECL

rlc_errno rlc_rx_init(struct rlc_context *ctx);
rlc_errno rlc_rx_deinit(struct rlc_context* ctx);

RLC_END_DECL

#endif /* RLC_RX_H__ */
