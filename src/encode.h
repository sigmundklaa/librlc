
#ifndef RLC_ENCODE_H__
#define RLC_ENCODE_H__

#include <rlc/rlc.h>

RLC_BEGIN_DECL

#define RLC_PDU_HEADER_MAX_SIZE (5)
#define RLC_STATUS_MAX_SIZE     (8)

void rlc_pdu_encode(const struct rlc_pdu *pdu, gabs_pbuf *buf,
                    enum rlc_service_type type, enum rlc_sn_width sn_width);

rlc_errno rlc_pdu_decode(struct rlc_pdu *pdu, gabs_pbuf *buf,
                         enum rlc_service_type type,
                         enum rlc_sn_width sn_width);

size_t rlc_pdu_header_size(const struct rlc_pdu *pdu,
                           enum rlc_service_type type,
                           enum rlc_sn_width sn_width);

void rlc_status_encode(const struct rlc_pdu_status *status, gabs_pbuf *buf,
                       enum rlc_sn_width sn_width);

rlc_errno rlc_status_decode(struct rlc_pdu_status *status, gabs_pbuf *buf,
                            enum rlc_sn_width sn_width);

size_t rlc_status_size(struct rlc_pdu_status *status,
                       enum rlc_sn_width sn_width);

RLC_END_DECL

#endif /* RLC_ENCODE_H__ */
