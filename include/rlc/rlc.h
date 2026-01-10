
#ifndef RLC_H__
#define RLC_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <gabs/pbuf.h>
#include <gabs/alloc.h>
#include <gabs/mutex.h>
#include <gabs/log.h>

#include <rlc/plat.h>
#include <rlc/decl.h>
#include <rlc/window.h>
#include <rlc/timer.h>
#include <rlc/segment.h>
#include <rlc/sdu.h>
#include <rlc/pdu.h>
#include <rlc/utils.h>
#include <rlc/rx.h>
#include <rlc/tx.h>

RLC_BEGIN_DECL

struct rlc_event {
        enum rlc_event_type {
                RLC_EVENT_RX_DONE,
                RLC_EVENT_RX_DONE_DIRECT,
                RLC_EVENT_RX_FAIL,

                RLC_EVENT_TX_DONE,
                RLC_EVENT_TX_FAIL
        } type;

        union {
                struct rlc_sdu *sdu;
                gabs_pbuf *buf; /* RX_DONE_DIRECT */
        };
};

struct rlc_methods {
        rlc_errno (*tx_submit)(struct rlc_context *, gabs_pbuf);
        rlc_errno (*tx_request)(struct rlc_context *);

        void (*event)(struct rlc_context *, const struct rlc_event *);
};

enum rlc_service_type {
        RLC_AM,
        RLC_UM,
        RLC_TM
};

struct rlc_config {
        enum rlc_service_type type;

        size_t window_size;
        size_t buffer_size;

        size_t pdu_without_poll_max;
        size_t byte_without_poll_max;

        uint32_t time_reassembly_us;
        uint32_t time_poll_retransmit_us;
        uint32_t time_status_prohibit_us;

        uint32_t max_retx_threshhold;

        enum rlc_sn_width sn_width;
};

typedef struct rlc_context {
        const struct rlc_config *conf;

        /* RLC specification state variables */
        struct {
                /* RX_NEXT_HIGHEST holds the value of the SN following
                 * the SN of the SDU with the highest SN among received
                 * SDUs. */
                uint32_t next_highest;

                /* Specification: RX_HIGHEST_STATUS holds the highest possible
                 * value of the SN which can be indicated by ACK_SN when
                 * constructing status PDU. */
                uint32_t highest_ack;

                /* RX_NEXT_STATUS_TRIGGER holds the value of the SN
                 * following the SN of the SDU which triggered
                 * reassembly. */
                uint32_t next_status_trigger;

                struct rlc_window win;
        } rx;
        struct {
                uint32_t next_sn; /* TX_Next in the spec */
                uint32_t retx_count;

                size_t pdu_without_poll;
                size_t byte_without_poll;

                struct rlc_window win;
        } tx;

        rlc_timer t_reassembly;
        rlc_timer t_poll_retransmit;
        rlc_timer t_status_prohibit;

        uint32_t poll_sn;
        bool force_poll;

        /* Generate status PDU on next available opportunity. AM only*/
        bool gen_status;

        gabs_mutex lock;

        struct rlc_sdu *sdus;

        const struct rlc_methods *methods;

        const gabs_logger_h *logger;
        const gabs_allocator_h *alloc_buf;
        const gabs_allocator_h *alloc_misc;

        rlc_platform platform;
} rlc_context;

rlc_errno rlc_init(struct rlc_context *ctx, const struct rlc_config *conf,
                   const struct rlc_methods *methods,
                   const gabs_allocator_h *misc_allocator,
                   const gabs_allocator_h *buf_allocator);

rlc_errno rlc_deinit(struct rlc_context *ctx);

rlc_errno rlc_reset(struct rlc_context *ctx);

RLC_END_DECL

#endif /* RLC_H__ */
