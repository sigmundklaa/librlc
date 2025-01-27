
#include <string.h>

#include "sdu.h"
#include "methods.h"

static bool seg_overlap_(const struct rlc_segment *left,
                         const struct rlc_segment *right)
{
        return right->start >= left->start && right->start <= left->end;
}

rlc_errno rlc_sdu_seg_append(struct rlc_context *ctx, struct rlc_sdu *sdu,
                             struct rlc_segment seg)
{
        struct rlc_sdu_segment *new_seg;
        struct rlc_sdu_segment *cur;
        struct rlc_sdu_segment *next;
        struct rlc_sdu_segment **lastp;

        lastp = &sdu->segments;
        next = NULL;

        for (rlc_each_node(sdu->segments, cur, next)) {
                next = cur->next;

                /* Seg overlaps with the element to the left */
                if (seg_overlap_(&cur->seg, &seg)) {
                        /* Overlaps to the right. Since overlap on both sides,
                         * they can be merged. */
                        if (next != NULL && seg_overlap_(&seg, &next->seg)) {
                                seg.end = next->seg.end;

                                cur->next = next->next;
                                rlc_dealloc(ctx, next);
                        }

                        seg.start = cur->seg.start;
                        goto entry_found;
                }

                /* If not overlapping to the left, but the offset is higher than
                 * that of the segment to the left */
                if (seg.start > cur->seg.end) {
                        if (next != NULL) {
                                if (seg.start >= next->seg.end) {
                                        continue;
                                } else if (seg.end >= next->seg.start) {
                                        /* Overlap to the right, merge */
                                        cur = next;
                                        seg.end = next->seg.end;

                                        goto entry_found;
                                }
                        }

                        /* Otherwise create a new entry and insert it */
                        lastp = &cur->next;
                        break;
                } else if (seg.start < cur->seg.start) {
                        if (seg.end >= cur->seg.start) {
                                seg.end = cur->seg.end;

                                goto entry_found;
                        }

                        /* Insert before current */
                        next = cur;
                        break;
                }

                lastp = &cur->next;
        }

        cur = rlc_alloc(ctx, sizeof(*cur));
        if (cur == NULL) {
                return -ENOMEM;
        }

        *lastp = cur;
        cur->next = next;

entry_found:
        (void)memcpy(&cur->seg, &seg, sizeof(seg));

        return 0;
}

void rlc_sdu_append(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        struct rlc_sdu **lastp;

        lastp = &ctx->sdus;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                lastp = &cur->next;
        }

        *lastp = sdu;
}

void rlc_sdu_remove(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        struct rlc_sdu **lastp;

        lastp = &ctx->sdus;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (cur == sdu) {
                        *lastp = sdu->next;

                        sdu->next = NULL;
                        break;
                }

                lastp = &sdu->next;
        }
}

struct rlc_sdu *rlc_sdu_alloc(struct rlc_context *ctx, enum rlc_sdu_dir dir)
{
        struct rlc_sdu *sdu;

        sdu = rlc_alloc(ctx, sizeof(*sdu));
        if (sdu == NULL) {
                return NULL;
        }

        sdu->dir = dir;

        if (sdu->dir == RLC_RX) {
                sdu->rx_buffer = rlc_alloc(ctx, ctx->conf->buffer_size);
                if (sdu->rx_buffer == NULL) {
                        rlc_dealloc(ctx, sdu);
                        return NULL;
                }

                sdu->rx_buffer_size = ctx->conf->buffer_size;
        }

        return sdu;
}

void rlc_sdu_dealloc_rx_buffer(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        if (sdu->dir != RLC_RX || sdu->rx_buffer == NULL) {
                return;
        }

        rlc_dealloc(ctx, sdu->rx_buffer);
        sdu->rx_buffer = NULL;
}

void rlc_sdu_dealloc(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu_segment *seg;

        rlc_sdu_dealloc_rx_buffer(ctx, sdu);

        for (rlc_each_node_safe(struct rlc_sdu_segment, sdu->segments, seg,
                                next)) {
                rlc_dealloc(ctx, seg);
        }

        rlc_dealloc(ctx, sdu);
}

struct rlc_sdu *rlc_sdu_get(struct rlc_context *ctx, uint32_t sn,
                            enum rlc_sdu_dir dir)
{
        struct rlc_sdu *sdu;

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir == dir && sdu->sn == sn) {
                        return sdu;
                }
        }

        return NULL;
}
