
#ifndef RLC_CONFIG_H__
#define RLC_CONFIG_H__

#include <stddef.h>
#include <stdint.h>

#include <rlc/utils.h>

RLC_BEGIN_DECL

enum rlc_sn_width {
        RLC_SN_6BIT,
        RLC_SN_12BIT,
        RLC_SN_18BIT,
};

enum rlc_service_type {
        RLC_AM,
        RLC_UM,
        RLC_TM
};

struct rlc_config {
        enum rlc_service_type type;

        size_t window_size;

        size_t pdu_without_poll_max;
        size_t byte_without_poll_max;

        uint32_t time_reassembly_us;
        uint32_t time_poll_retransmit_us;
        uint32_t time_status_prohibit_us;

        uint32_t max_retx_threshhold;

        enum rlc_sn_width sn_width;
};

RLC_END_DECL

#endif /* RLC_CONFIG_H__ */
