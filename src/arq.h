
#ifndef RLC_ARQ_H__
#define RLC_ARQ_H__

#include <rlc/rlc.h>

RLC_BEGIN_DECL

rlc_errno rlc_arq_init(struct rlc_context *ctx);
rlc_errno rlc_arq_deinit(struct rlc_context *ctx);

ssize_t rlc_arq_tx_status(struct rlc_context *ctx, size_t max_size);
size_t rlc_arq_tx_yield(struct rlc_context *ctx, size_t max_size);
bool rlc_arq_tx_pollable(const struct rlc_context *ctx,
                         const struct rlc_sdu *sdu);
void rlc_arq_tx_register(struct rlc_context *ctx, const struct rlc_pdu *pdu);

rlc_buf *rlc_arq_rx_status(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                           rlc_buf *buf);

void rlc_arq_rx_register(struct rlc_context *ctx, const struct rlc_pdu *pdu);

RLC_END_DECL

#endif /* RLC_ARQ_H__ */
