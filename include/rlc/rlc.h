
#ifndef RLC_H__
#define RLC_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

#ifdef __cplusplus
#define RLC_BEGIN_DECL extern "C" {
#define RLC_END_DECL   }
#else
#define RLC_BEGIN_DECL
#define RLC_END_DECL
#endif

RLC_BEGIN_DECL

typedef int32_t rlc_errno;
#define RLC_PRI_ERRNO PRIi32

#include <semaphore.h>
typedef sem_t rlc_sem;

/** @cond PRIVATE */
struct rlc_context;
struct rlc_chunk;
struct rlc_sdu;
struct rlc_event;
/** @endcond */

struct rlc_methods {
        rlc_errno (*tx_submit)(struct rlc_context *, struct rlc_chunk *,
                               size_t);
        rlc_errno (*tx_request)(struct rlc_context *);

        void (*event)(const struct rlc_context *, struct rlc_event *);

        void *(*mem_alloc)(struct rlc_context *, size_t);
        void (*mem_dealloc)(struct rlc_context *, void *);
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

typedef struct rlc_context {
        struct {
                uint8_t *mem;
                size_t size;
        } workbuf;

        enum rlc_sdu_type type;
        enum rlc_sn_width sn_width;

        size_t window_size;
        size_t buffer_size;

        /* RLC specification state variables */
        uint32_t tx_next;

        struct rlc_sdu *sdus;
        const struct rlc_methods *methods;
} rlc_context;

typedef struct rlc_sdu {
        enum rlc_sdu_dir {
                RLC_TX,
                RLC_RX,
        } dir;

        enum rlc_sdu_state {
                RLC_READY,
                RLC_RESEND,
                RLC_DOACK,
                RLC_WAITACK,
        } state;

        /* RLC specification state variables */
        uint32_t tx_offset_unack; /* Last unacknowledged offset */
        uint32_t tx_offset_ack;   /* Last acknowledged offset */
        uint32_t sn;

        uint32_t rx_pos;

        const struct rlc_chunk *chunks;
        size_t num_chunks;

        void *rx_buffer;
        size_t rx_buffer_size;

        struct rlc_sdu *next;
} rlc_sdu;

struct rlc_pdu {
        enum rlc_sdu_type type;

        size_t size;

        uint32_t sn;
        uint32_t seg_offset;

        struct {
                bool is_first: 1;
                bool is_last: 1;

                bool polled: 1;
        } flags;
};

struct rlc_chunk {
        void *data;
        size_t size;
};

struct rlc_event {
        enum rlc_event_type {
                RLC_EVENT_RX_DONE,
                RLC_EVENT_RX_FAIL,
        } type;

        union {
                struct rlc_chunk rx_done;
        } data;
};

#define rlc_each_node(start_, tptr_, prop_name_)                               \
        tptr_ = start_;                                                        \
        tptr_ != NULL;                                                         \
        tptr_ = tptr_->prop_name_

#define rlc_each_item(arr_, cur_, len_)                                        \
        cur_ = &arr_[0];                                                       \
        cur_ < &arr_[len_];                                                    \
        cur_++

rlc_errno rlc_init(struct rlc_context *ctx, enum rlc_sdu_type type,
                   size_t window_size, size_t buffer_size,
                   const struct rlc_methods *methods);

rlc_errno rlc_send(rlc_context *ctx, rlc_sdu *sdu,
                   const struct rlc_chunk *chunks, size_t num_chunks);

void rlc_tx_avail(struct rlc_context *ctx, size_t size);

void rlc_rx_submit(struct rlc_context *ctx, struct rlc_chunk *chunks,
                   size_t num_chunks);

RLC_END_DECL

#endif /* RLC_H__ */
