
#ifndef RLC_LOG_H__
#define RLC_LOG_H__

#include <rlc/decl.h>
#include <gabs/log.h>

RLC_BEGIN_DECL

struct rlc_sdu;
struct rlc_context;

void rlc_log_tx_window(struct rlc_context *ctx);
void rlc_log_rx_window(struct rlc_context *ctx);

void rlc_log_sdu(const gabs_logger_h *logger, const struct rlc_sdu *sdu);

RLC_END_DECL

#endif /* RLC_LOG_H__ */
