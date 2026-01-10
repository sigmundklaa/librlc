
#ifndef RLC_TX_H__
#define RLC_TX_H__

#include <gabs/pbuf.h>

#include <rlc/errno.h>

RLC_BEGIN_DECL

struct rlc_context;
struct rlc_sdu;

rlc_errno rlc_tx(struct rlc_context *ctx, gabs_pbuf buf, struct rlc_sdu **sdu);

size_t rlc_tx_avail(struct rlc_context *ctx, size_t size);

size_t rlc_tx_yield(struct rlc_context *ctx, size_t max_size);

RLC_END_DECL

#endif /* RLC_TX_H__ */
