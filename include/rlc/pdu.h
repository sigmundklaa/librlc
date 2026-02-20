
#ifndef RLC_PDU_H__
#define RLC_PDU_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <rlc/decl.h>
#include <rlc/seg_list.h>

RLC_BEGIN_DECL

#define RLC_STATUS_SO_MAX (UINT16_MAX)
#define RLC_STATUS_SO_MIN (0x0)

struct rlc_pdu {
        size_t size;

        uint32_t sn;
        uint32_t seg_offset;

        struct {
                bool is_first: 1;
                bool is_last: 1;

                bool polled: 1;

                bool ext: 1;       /* AM Status PDU E1 bit */
                bool is_status: 1; /* True if PDU is AM status PDU */
        } flags;
};

/** @brief Optional status payload following a PDU Status header */
struct rlc_pdu_status {
        struct rlc_seg offset;

        uint32_t range;
        uint32_t nack_sn;

        struct {
                bool has_more: 1;
                bool has_range: 1;
                bool has_offset: 1;
        } ext;

        struct rlc_pdu_status *next;
};

RLC_END_DECL

#endif /* RLC_PDU_H__ */
