
#ifndef RLC_LOG_H__
#define RLC_LOG_H__

#include <rlc/utils.h>
#include <gabs/log.h>

RLC_BEGIN_DECL

struct rlc_sdu;
struct rlc_context;

void rlc_log_tx_window(struct rlc_context *ctx);
void rlc_log_rx_window(struct rlc_context *ctx);

void rlc_log_tx_sdu(const gabs_logger_h *logger, struct rlc_sdu *sdu);
void rlc_log_rx_sdu(const gabs_logger_h *logger, struct rlc_sdu *sdu);

RLC_END_DECL

#endif /* RLC_LOG_H__ */
