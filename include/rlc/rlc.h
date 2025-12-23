
#ifndef RLC_H__
#define RLC_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <gnb/gnb.h>

#include <rlc/plat.h>
#include <rlc/decl.h>
#include <rlc/window.h>
#include <rlc/timer.h>
#include <rlc/sync.h>

RLC_BEGIN_DECL

/** @cond PRIVATE */
struct rlc_context;
struct rlc_sdu;
struct rlc_event;
/** @endcond */

#define RLC_STATUS_SO_MAX (UINT16_MAX)
#define RLC_STATUS_SO_MIN (0x0)

enum rlc_alloc_type {
        RLC_ALLOC_SDU,
        RLC_ALLOC_BUF,
        RLC_ALLOC_SDU_SEGMENT
};

struct rlc_methods {
        rlc_errno (*tx_submit)(struct rlc_context *, gnb_h);
        rlc_errno (*tx_request)(struct rlc_context *);

        void (*event)(struct rlc_context *, const struct rlc_event *);

        void *(*mem_alloc)(struct rlc_context *, size_t, enum rlc_alloc_type);
        void (*mem_dealloc)(struct rlc_context *, void *, enum rlc_alloc_type);
};

enum rlc_sdu_type {
        RLC_AM,
        RLC_UM,
        RLC_TM
};

enum rlc_sn_width {
        RLC_SN_6BIT,
        RLC_SN_12BIT,
        RLC_SN_18BIT,
};

struct rlc_segment {
        uint32_t start;
        uint32_t end;
};

struct rlc_config {
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
        enum rlc_sdu_type type;

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

        rlc_lock lock;

        /* Generate status PDU on next available opportunity. AM only*/
        bool gen_status;

        struct rlc_sdu *sdus;
        const struct rlc_methods *methods;

        struct gnb_allocator alloc_gnb;

        void *user_data;
} rlc_context;

struct rlc_sdu_segment {
        struct rlc_segment seg;
        struct rlc_sdu_segment *next;
};

typedef struct rlc_sdu {
        enum rlc_sdu_dir {
                RLC_TX,
                RLC_RX,
        } dir;

        enum rlc_sdu_state {
                RLC_READY,
                RLC_WAIT,
                RLC_DONE,
        } state;

        struct {
                bool rx_last_received: 1;
        } flags;

        unsigned int refcount;

        /* RLC specification state variables */
        uint32_t sn;
        unsigned int retx_count; /* Number of retransmissions */

        gnb_h buffer;

        rlc_errno tx_status;
        rlc_sem tx_sem;

        /* TX mode: unsent segments */
        /* RX mode: received segments */
        struct rlc_sdu_segment *segments;

        struct rlc_sdu *next;
} rlc_sdu;

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
        struct rlc_segment offset;

        uint32_t range;
        uint32_t nack_sn;

        struct {
                bool has_more: 1;
                bool has_range: 1;
                bool has_offset: 1;
        } ext;

        struct rlc_pdu_status *next;
};

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
                gnb_h *buf; /* RX_DONE_DIRECT */
        };
};

#define rlc_array_size(x) (sizeof(x) / sizeof((x)[0]))

#define rlc_each_node(start_, tptr_, prop_name_)                               \
        tptr_ = start_;                                                        \
        tptr_ != NULL;                                                         \
        tptr_ = tptr_->prop_name_

#define rlc_concat3__(x, y, z) x##y##z
#define rlc_ens_safe__(name_)  rlc_concat3__(rlc_ens_, name_, __LINE__)

/**
 * @brief Iterate over each node in a linked list, ensuring that it is still
 * safe to iterate even if the current node is removed from the list during
 * iteration.
 *
 * To do this two temporary variables are used during the iteration, which
 * requires the caller to pass @p type_ in as an argument
 *
 * @param type_ Type of item being iterated over
 * @param start_ Head of linked list
 * @param tptr_ Target pointer; pointer holding the current item
 * @param prop_name_ Property name of the next entry in @p type_
 */
#define rlc_each_node_safe(type_, start_, tptr_, prop_name_)                   \
        type_ *rlc_ens_safe__(cur__) = start_,                                 \
              *rlc_ens_safe__(next__) =                                        \
                      start_ == NULL ? NULL : start_->prop_name_;              \
        (tptr_ = rlc_ens_safe__(cur__)) != NULL;                               \
        rlc_ens_safe__(cur__) = rlc_ens_safe__(next__),                        \
        rlc_ens_safe__(next__) = rlc_ens_safe__(next__) == NULL                \
                                         ? NULL                                \
                                         : rlc_ens_safe__(next__)->prop_name_

#define rlc_each_item(arr_, cur_)                                              \
        cur_ = &arr_[0];                                                       \
        cur_ < &arr_[rlc_array_size(arr_)];                                    \
        cur_++

rlc_errno rlc_init(struct rlc_context *ctx, enum rlc_sdu_type type,
                   const struct rlc_config *conf,
                   const struct rlc_methods *methods, void *user_data);

rlc_errno rlc_deinit(struct rlc_context *ctx);

rlc_errno rlc_reset(struct rlc_context *ctx);

rlc_errno rlc_send(rlc_context *ctx, gnb_h buf, struct rlc_sdu **sdu);

size_t rlc_tx_avail(struct rlc_context *ctx, size_t size);

void rlc_rx_submit(struct rlc_context *ctx, gnb_h buf);

static inline void *rlc_user_data(struct rlc_context *ctx)
{
        return ctx->user_data;
}

static inline bool rlc_segment_okay(struct rlc_segment *segment)
{
        return segment->start != 0 || segment->end != 0;
}

RLC_END_DECL

#endif /* RLC_H__ */
