
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

/**
 * @brief Insert segment into segment list.
 *
 * This function takes into account that parts of the segment is already
 * received, and adjusts the list to only represent each byte in the SDU once.
 *
 * It is safe to call this function with a segment that may span multiple
 * segments already in the list. In that case, the first part of the segment
 * that can be inserted is returned in @p unique, and @p seg is updated with the
 * remaining parts of the segment. When `seg->start == 0` and `seg->end == 0` it
 * should not be called again.
 *
 * @param ctx
 * @param sdu
 * @param seg Pointer to segment to insert. If the segment must be split into
 * seperate areas, this is updated with the remaining parts of the segment that
 * is not represented in the returned segment.
 * @param unique Pointer where adjusted segment will be stored.
 * @return rlc_errno
 * @retval -ENODATA No unique data in @p seg
 * @retval -ENOMEM Unable to allocate memory for segment
 */
rlc_errno rlc_sdu_seg_insert(struct rlc_context *ctx, struct rlc_sdu *sdu,
                             struct rlc_segment *seg,
                             struct rlc_segment *unique);

static inline rlc_errno rlc_sdu_seg_insert_all(struct rlc_context *ctx,
                                               struct rlc_sdu *sdu,
                                               struct rlc_segment seg)
{
        rlc_errno status;
        struct rlc_segment unique;

        do {
                status = rlc_sdu_seg_insert(ctx, sdu, &seg, &unique);
        } while (rlc_segment_okay(&unique) && rlc_segment_okay(&seg));

        return status;
}

size_t rlc_sdu_count(struct rlc_context *ctx, enum rlc_sdu_dir dir);

void rlc_sdu_insert(struct rlc_context *ctx, struct rlc_sdu *sdu);

void rlc_sdu_remove(struct rlc_context *ctx, struct rlc_sdu *sdu);

struct rlc_sdu *rlc_sdu_alloc(struct rlc_context *ctx, enum rlc_sdu_dir dir);

void rlc_sdu_dealloc_buffer(struct rlc_context *ctx, struct rlc_sdu *sdu);

void rlc_sdu_incref(struct rlc_sdu *sdu);

void rlc_sdu_decref(struct rlc_context *ctx, struct rlc_sdu *sdu);

struct rlc_sdu *rlc_sdu_get(struct rlc_context *ctx, uint32_t sn,
                            enum rlc_sdu_dir dir);

RLC_END_DECL

#endif /* RLC_SDU_H__ */
