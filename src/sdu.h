
#ifndef RLC_SDU_H__
#define RLC_SDU_H__

#include <rlc/utils.h>
#include <rlc/rlc.h>

RLC_BEGIN_DECL

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
        return sdu->segments->next != NULL || sdu->segments->seg.start != 0;
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
        /* Last received and exactly one segment */
        return sdu->flags.rx_last_received && sdu->segments != NULL &&
               sdu->segments->seg.start == 0 && sdu->segments->next == NULL;
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
        return sdu->segments->next != NULL || sdu->segments->seg.start != 0;
}

rlc_errno rlc_sdu_seg_append(struct rlc_context *ctx, struct rlc_sdu *sdu,
                             struct rlc_segment seg);

void rlc_sdu_append(struct rlc_context *ctx, struct rlc_sdu *sdu);

void rlc_sdu_remove(struct rlc_context *ctx, struct rlc_sdu *sdu);

struct rlc_sdu *rlc_sdu_alloc(struct rlc_context *ctx, enum rlc_sdu_dir dir);

void rlc_sdu_dealloc_buffer(struct rlc_context *ctx, struct rlc_sdu *sdu);

void rlc_sdu_dealloc(struct rlc_context *ctx, struct rlc_sdu *sdu);

struct rlc_sdu *rlc_sdu_get(struct rlc_context *ctx, uint32_t sn,
                            enum rlc_sdu_dir dir);

RLC_END_DECL

#endif /* RLC_SDU_H__ */
