
#ifndef RLC_ENCODE_H__
#define RLC_ENCODE_H__

#include <rlc/rlc.h>

RLC_BEGIN_DECL

#define RLC_PDU_HEADER_MAX_SIZE (5)
#define RLC_STATUS_MAX_SIZE     (8)

void rlc_pdu_encode(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                    gnb_h *buf);

rlc_errno rlc_pdu_decode(struct rlc_context *ctx, struct rlc_pdu *pdu,
                         gnb_h *buf);

size_t rlc_pdu_header_size(const struct rlc_context *ctx,
                           const struct rlc_pdu *pdu);

void rlc_status_encode(struct rlc_context *ctc,
                       const struct rlc_pdu_status *status, gnb_h *buf);

rlc_errno rlc_status_decode(struct rlc_context *ctx,
                            struct rlc_pdu_status *status, gnb_h *buf);

size_t rlc_status_size(const struct rlc_context *ctx,
                       struct rlc_pdu_status *status);

RLC_END_DECL

#endif /* RLC_ENCODE_H__ */
