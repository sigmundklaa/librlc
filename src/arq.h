
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
 * @brief Determine if the next PDU for @p sdu should poll
 *
 * Returns `true` if:
 * - `type` == `RLC_AM` and
 *   - Retransmitted (not the last segment in the list) or
 *   - Bytes without poll exceeded threshhold or
 *   - PDU without poll exceeded threshhold or
 *   - Last segment
 *
 * @param ctx Context
 * @param sdu
 * @return bool
 * @retval true Should Poll
 * @retval false Shouldn't poll
 */
bool rlc_arq_tx_pollable(const struct rlc_context *ctx,
                         const struct rlc_sdu *sdu);

/**
 * @brief Register @p pdu as being transmitted.
 *
 * This updates the internal ARQ state depending on @p pdu.
 */
void rlc_arq_tx_register(struct rlc_context *ctx, const struct rlc_pdu *pdu);

/**
 * @brief Receive status PDU
 *
 * @param ctx
 * @param pdu
 * @param buf Buffer with status segments following the header
 * @return gnb_h* @p buf (potentially modified).
 */
gnb_h *rlc_arq_rx_status(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                           gnb_h *buf);

/**
 * @brief Register @p pdu as being received.
 *
 * This updates the internal ARQ state depending on @p pdu.
 */
void rlc_arq_rx_register(struct rlc_context *ctx, const struct rlc_pdu *pdu);

RLC_END_DECL

#endif /* RLC_ARQ_H__ */
