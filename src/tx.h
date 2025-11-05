
#ifndef RLC_TX_H__
#define RLC_TX_H__

#include <rlc/rlc.h>

RLC_BEGIN_DECL

size_t rlc_tx_yield(struct rlc_context *ctx, size_t max_size);

RLC_END_DECL

#endif /* RLC_TX_H__ */
