
#ifndef RLC_SDU_H__
#define RLC_SDU_H__

#include <stdint.h>
#include <stdbool.h>

#include <gabs/pbuf.h>

#include <rlc/errno.h>
#include <rlc/decl.h>
#include <rlc/list.h>
#include <rlc/seg_buf.h>
#include <rlc/utils.h>

struct rlc_context;

RLC_BEGIN_DECL

enum rlc_sn_width {
        RLC_SN_6BIT,
        RLC_SN_12BIT,
        RLC_SN_18BIT,
};

enum rlc_sdu_state {
        RLC_READY,
        RLC_WAIT,
        RLC_DONE,
};

typedef struct rlc_sdu {
        enum rlc_sdu_state state;

        unsigned int refcount;

        /* RLC specification state variables */
        uint32_t sn;

        union {
                struct {
                        gabs_pbuf buffer;
                        rlc_seg_list unsent;

                        unsigned int retx_count; /* Number of retransmissions */
                } tx;
                struct {
                        struct rlc_seg_buf buffer;
                        bool last_received;
                } rx;
        };
        bool is_tx;

        struct rlc_context *ctx;
        rlc_list_node list_node;
} rlc_sdu;

typedef rlc_list rlc_sdu_queue;

#define rlc_sdu_from_it(it_) rlc_list_it_item(it_, struct rlc_sdu, list_node)

/** @brief Allocate SDU with direction @p dir */
struct rlc_sdu *rlc_sdu_alloc(struct rlc_context *ctx, bool is_tx);

/** @brief Increase reference count of @p sdu */
void rlc_sdu_incref(struct rlc_sdu *sdu);

/** @brief Decrease reference count of @p sdu, deallocting if reaching 0 */
void rlc_sdu_decref(struct rlc_sdu *sdu);

void rlc_sdu_queue_clear(rlc_sdu_queue *q);

static inline struct rlc_sdu *rlc_sdu_queue_head(rlc_sdu_queue *q)
{
        return rlc_sdu_from_it(rlc_list_it_init(q));
}

/** @brief Get SDU with SN=@p sn */
struct rlc_sdu *rlc_sdu_queue_get(rlc_sdu_queue *queue, uint32_t sn);

/** @brief Insert SDU into SDU list */
void rlc_sdu_queue_insert(rlc_sdu_queue *queue, struct rlc_sdu *sdu);

/** @brief Remove SDU from SDU list */
void rlc_sdu_queue_remove(rlc_sdu_queue *queue, struct rlc_sdu *sdu);

/**
 * @brief Check if parts of @p sdu has been submitted to the lower layer
 *
 * An SDU has been submitted to a lower layer if:
 * - it contains more than one segment or
 * - its only segment does not start at 0
 *
 * @param sdu
 * @return bool
 * @retval true Not yet been submitted to lower layer
 * @retval false Been submitted to lower layer
 */
static inline bool rlc_sdu_submitted(struct rlc_sdu *sdu)
{
        rlc_assert(sdu->is_tx);

        rlc_list_it it;
        struct rlc_seg_item *item;

        it = rlc_list_it_init(&sdu->tx.unsent);
        item = rlc_seg_item_from_it(it);

        return !rlc_list_it_eoi(rlc_list_it_next(it)) || item->seg.start != 0;
}

/**
 * @brief Check if SDU has been received in full
 *
 * An SDU is received in full if:
 * - the last segment has been received and
 * - there is exactly one segment and
 * - the one segment starts at 0
 *
 * @param sdu
 * @return bool
 * @retval true Receieved in full
 * @retval false Not received in full
 */
static inline bool rlc_sdu_is_rx_done(struct rlc_sdu *sdu)
{
        rlc_assert(!sdu->is_tx);

        rlc_list_it it;
        struct rlc_seg_item *item;

        it = rlc_list_it_init(&sdu->rx.buffer.segments);
        item = rlc_seg_item_from_it(it);

        /* Last received and exactly one segment */
        return sdu->rx.last_received && !rlc_list_it_eoi(it) &&
               item->seg.start == 0 && rlc_list_it_eoi(rlc_list_it_next(it));
}

/**
 * @brief Check if loss has been detected yet on a receiving SDU
 *
 * A loss is detected when an SDU contains more than one segments. If all
 * segments are continous from 0 a loss is not detected. This means that if
 * either:
 * - The SDU only has more than one segment or
 * - The first segment does not start at 0
 * a loss is detected.
 *
 * @param sdu
 * @return bool
 * @retval true Loss has been detected
 * @retval false Loss has not been detected
 */
static inline bool rlc_sdu_loss_detected(struct rlc_sdu *sdu)
{
        rlc_assert(!sdu->is_tx);

        rlc_list_it it;
        struct rlc_seg_item *item;

        it = rlc_list_it_init(&sdu->rx.buffer.segments);
        item = rlc_seg_item_from_it(it);

        return !rlc_list_it_eoi(rlc_list_it_next(it)) || item->seg.start != 0;
}

RLC_END_DECL

#endif /* RLC_SDU_H__ */
