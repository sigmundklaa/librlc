
#include <rlc/buf.h>

#include <string.h>

#include "sdu.h"
#include "methods.h"

static bool seg_overlap_(const struct rlc_segment *left,
                         const struct rlc_segment *right)
{
        return right->start >= left->start && right->start <= left->end;
}

/* TODO: rename to insert */
struct rlc_segment rlc_sdu_seg_append(struct rlc_context *ctx,
                                      struct rlc_sdu *sdu,
                                      struct rlc_segment seg)
{
        struct rlc_sdu_segment *cur;
        struct rlc_sdu_segment *next;
        struct rlc_sdu_segment **lastp;
        struct rlc_segment ret_seg;

        lastp = &sdu->segments;
        next = NULL;
        ret_seg = seg;

        for (rlc_each_node(sdu->segments, cur, next)) {
                next = cur->next;

                /* Seg overlaps with the element to the left */
                if (seg_overlap_(&cur->seg, &seg)) {
                        /* Overlaps to the right. Since overlap on both sides,
                         * they can be merged. */
                        if (next != NULL && seg_overlap_(&seg, &next->seg)) {
                                seg.end = next->seg.end;
                                ret_seg.end = next->seg.start;

                                cur->next = next->next;
                                rlc_dealloc(ctx, next, RLC_ALLOC_SDU_SEGMENT);
                        }

                        ret_seg.start = cur->seg.end;
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
                                        ret_seg.end = next->seg.start;

                                        goto entry_found;
                                }
                        }

                        /* Otherwise create a new entry and insert it */
                        lastp = &cur->next;
                        break;
                } else if (seg.start < cur->seg.start) {
                        if (seg.end >= cur->seg.start) {
                                seg.end = cur->seg.end;
                                ret_seg.end = cur->seg.start;

                                goto entry_found;
                        }

                        /* Insert before current */
                        next = cur;
                        break;
                }

                lastp = &cur->next;
        }

        cur = rlc_alloc(ctx, sizeof(*cur), RLC_ALLOC_SDU_SEGMENT);
        if (cur == NULL) {
                rlc_assert(0);
                return (struct rlc_segment){0};
        }

        *lastp = cur;
        cur->next = next;

entry_found:
        (void)memcpy(&cur->seg, &seg, sizeof(seg));

        return ret_seg;
}

size_t rlc_sdu_count(struct rlc_context *ctx, enum rlc_sdu_dir dir)
{
        size_t count;
        struct rlc_sdu *sdu;

        count = 0;
        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir == dir) {
                        count++;
                }
        }

        return count;
}

void rlc_sdu_insert(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        struct rlc_sdu **lastp;

        lastp = &ctx->sdus;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (sdu->sn <= cur->sn) {
                        assert(sdu->sn != cur->sn);

                        sdu->next = cur;
                        break;
                }

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

                lastp = &cur->next;
        }
}

struct rlc_sdu *rlc_sdu_alloc(struct rlc_context *ctx, enum rlc_sdu_dir dir,
                              rlc_buf *buf)
{
        struct rlc_sdu *sdu;

        sdu = rlc_alloc(ctx, sizeof(*sdu), RLC_ALLOC_SDU);
        if (sdu == NULL) {
                return NULL;
        }

        sdu->dir = dir;
        sdu->refcount = 1;

        rlc_buf_incref(buf);
        sdu->buffer = buf;

        return sdu;
}

void rlc_sdu_dealloc_buffer(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        if (sdu->buffer == NULL) {
                return;
        }

        rlc_buf_decref(sdu->buffer, ctx);
        sdu->buffer = NULL;
}

void rlc_sdu_incref(struct rlc_sdu *sdu)
{
        sdu->refcount++;
}

void rlc_sdu_decref(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu_segment *seg;

        if (--sdu->refcount == 0) {
                rlc_sdu_dealloc_buffer(ctx, sdu);

                for (rlc_each_node_safe(struct rlc_sdu_segment, sdu->segments,
                                        seg, next)) {
                        rlc_dealloc(ctx, seg, RLC_ALLOC_SDU_SEGMENT);
                }

                rlc_dealloc(ctx, sdu, RLC_ALLOC_SDU);
        }
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
