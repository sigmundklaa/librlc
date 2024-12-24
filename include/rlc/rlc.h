
#ifndef RLC_H__
#define RLC_H__

#include <stdint.h>
#include <stddef.h>
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
struct rlc_transfer;
/** @endcond */

struct rlc_methods {
        rlc_errno (*tx_submit)(struct rlc_context *, struct rlc_chunk *,
                               size_t);
        rlc_errno (*tx_request)(struct rlc_context *);

        rlc_errno (*tx_event)(struct rlc_context *);
        rlc_errno (*rx_event)(struct rlc_context *);

        void *(*mem_alloc)(struct rlc_context *, size_t);
        void (*mem_dealloc)(struct rlc_context *, void *);
};

enum rlc_transfer_type {
        RLC_AM,
        RLC_UM,
        RLC_TM
};

typedef struct rlc_context {
        struct {
                uint8_t *mem;
                size_t size;
        } workbuf;

        enum rlc_transfer_type type;

        size_t window_size;
        size_t buffer_size;

        /* RLC specification state variables */
        uint32_t tx_next;

        struct rlc_transfer *transfers;
        const struct rlc_methods *methods;
} rlc_context;

typedef struct rlc_transfer {
        enum rlc_transfer_dir {
                RLC_TX,
                RLC_RX,
        } dir;

        enum rlc_transfer_state {
                RLC_READY,
                RLC_RESEND,
                RLC_DOACK,
                RLC_WAITACK,
        } state;

        /* RLC specification state variables */
        uint32_t tx_offset_unack; /* Last unacknowledged offset */
        uint32_t tx_offset_ack;   /* Last acknowledged offset */
        uint32_t sn;

        const struct rlc_chunk *chunks;
        size_t num_chunks;

        void *rx_buffer;
        size_t rx_buffer_size;

        struct rlc_transfer *next;
} rlc_transfer;

struct rlc_pdu {
        enum rlc_transfer_type type;

        size_t size;

        uint32_t sn;
        uint32_t seg_offset;
};

#define rlc_each_node(start_, tptr_, prop_name_)                               \
        tptr_ = start_;                                                        \
        tptr_ != NULL;                                                         \
        tptr_ = tptr_->prop_name_

#define rlc_each_item(arr_, cur_, len_)                                        \
        cur_ = &arr_[0];                                                       \
        cur_ < &arr_[len_];                                                    \
        cur_++

rlc_errno rlc_init(struct rlc_context *ctx, enum rlc_transfer_type type,
                   size_t window_size, size_t buffer_size,
                   const struct rlc_methods *methods);

rlc_errno rlc_send(rlc_context *ctx, rlc_transfer *transfer,
                   const struct rlc_chunk *chunks, size_t num_chunks);

void rlc_tx_avail(struct rlc_context *ctx, size_t size);

RLC_END_DECL

#endif /* RLC_H__ */
