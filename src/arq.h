
#ifndef RLC_ARQ_H__
#define RLC_ARQ_H__

#include <rlc/rlc.h>

RLC_BEGIN_DECL

rlc_errno rlc_arq_init(struct rlc_context *ctx);
rlc_errno rlc_arq_deinit(struct rlc_context *ctx);

/**
 * @brief Yield a transmit opportunity of @p max_size to ARQ.
 *
 * This will generate and send status PDUs for the SDUs that require it.
 * @param ctx
 * @param max_size
 * @return size_t Number of bytes used
 */
size_t rlc_arq_tx_yield(struct rlc_context *ctx, size_t max_size);

/**
 * @brief "Fill" PDU with ARQ contents. This essentially just modifies the
 * poll bit, and handles state variables.
 */
void rlc_arq_tx_pdu_fill(struct rlc_context *ctx, struct rlc_sdu *sdu,
                         struct rlc_pdu *pdu);

/**
 * @brief Receive status PDU
 *
 * @param ctx
 * @param pdu
 * @param buf Buffer with status segments following the header
 * @return gabs_pbuf* @p buf (potentially modified).
 */
gabs_pbuf *rlc_arq_rx_status(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                             gabs_pbuf *buf);

/**
 * @brief Register @p pdu as being received.
 *
 * This updates the internal ARQ state depending on @p pdu.
 */
void rlc_arq_rx_register(struct rlc_context *ctx, const struct rlc_pdu *pdu);

RLC_END_DECL

#endif /* RLC_ARQ_H__ */
