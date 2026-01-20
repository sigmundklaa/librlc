
#include <rlc/rlc.h>
#include <rlc/sdu.h>
#include <rlc/list.h>

#include <string.h>

#include "methods.h"
#include "log.h"

/** @brief Check if the start of @p right lies within the range of @p left */
static bool seg_overlap(const struct rlc_segment *left,
                        const struct rlc_segment *right)
{
        return right->start >= left->start && right->start <= left->end;
}

rlc_errno rlc_sdu_seg_insert(struct rlc_context *ctx, struct rlc_sdu *sdu,
                             struct rlc_segment *segptr,
                             struct rlc_segment *unique)
{
        struct rlc_sdu_segment *slot;
        struct rlc_sdu_segment *left;
        struct rlc_sdu_segment *right;
        struct rlc_sdu_segment **lastp;
        struct rlc_segment seg;
        bool overlap_left;
        bool overlap_right;

        lastp = &sdu->segments;
        left = NULL;
        right = NULL;

        seg = *segptr;

        for (rlc_each_node(sdu->segments, left, next)) {
                right = left->next;

                overlap_left = seg_overlap(&left->seg, &seg);
                overlap_right = right && seg_overlap(&seg, &right->seg);

                /* Overlapping with either the left or right segment. In that
                 * case, we need to insert atleast a chunk of the segment now.
                 *
                 * NOTE: This moves the start of the segment if overlapping to
                 * the left, and the end of the segment if overlapping to the
                 * right. If the segment is completely contained within
                 * either left or right, this will cause seg.start>=seg.end.
                 * This case should be (and is) handled before returning.
                 */
                if (overlap_left || overlap_right) {
                        if (overlap_left) {
                                seg.start = left->seg.end;
                        }

                        if (overlap_right) {
                                seg.end = right->seg.start;
                        }

                        lastp = &left->next;
                        break;
                }

                /* Not overlapping, but should be inserted before the left
                 * neighbour, meaning we can't continue iterating from here. */
                if (seg.start <= left->seg.start) {
                        seg.end = rlc_min(left->seg.start, seg.end);

                        right = left;
                        left = NULL;

                        break;
                }

                lastp = &left->next;
        }

        /* Error check. This happens if the segment is completely contained
         * within the left or right neighbours and the segment should
         * then be completely discarded. */
        if (seg.start >= seg.end) {
                *segptr = (struct rlc_segment){0};
                *unique = *segptr;

                return -ENODATA;
        }

        *unique = seg;
        slot = NULL;

        /* New segment and the right neighbour can be merged together. */
        if (right != NULL && seg.end >= right->seg.start) {
                seg.end = right->seg.end;
                right->seg = seg;

                slot = right;
        }

        /* New segment can be merged into left one */
        if (left != NULL && seg.start <= left->seg.end) {
                left->seg.end = seg.end;

                /* Already merged with right, so we are now merging with two,
                 * meaning one can be deleted. In this case, we delete the right
                 * one */
                if (slot != NULL) {
                        left->next = slot->next;
                        rlc_dealloc(ctx, slot);
                }

                slot = left;
        }

        if (slot == NULL) {
                slot = rlc_alloc(ctx, sizeof(*slot));
                if (slot == NULL) {
                        rlc_assert(0);
                        return -ENOMEM;
                }

                slot->seg = seg;
                slot->next = right;

                *lastp = slot;
        }

        segptr->start = slot->seg.end;
        if (segptr->start >= segptr->end) {
                segptr->start = 0;
                segptr->end = 0;
        }

        return 0;
}

size_t rlc_sdu_seg_byte_offset(const struct rlc_sdu *sdu, size_t start)
{
        const struct rlc_sdu_segment *cur;
        size_t ret;

        ret = 0;

        for (rlc_each_node(sdu->segments, cur, next)) {
                if (cur->seg.start >= start) {
                        break;
                } else if (cur->seg.end >= start) {
                        ret += start - cur->seg.start;
                        break;
                }

                ret += cur->seg.end - cur->seg.start;
        }

        return ret;
}

void rlc_sdu_insert(rlc_sdu_queue *q, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        rlc_list_it it;

        rlc_list_foreach(q, it)
        {
                cur = rlc_sdu_from_it(it);

                if (sdu->sn <= cur->sn) {
                        break;
                }
        }

        (void)rlc_list_it_put_front(it, &sdu->list_node);
}

void rlc_sdu_remove(rlc_sdu_queue *q, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        rlc_list_it it;

        rlc_list_foreach(q, it)
        {
                cur = rlc_sdu_from_it(it);

                if (cur == sdu) {
                        (void)rlc_list_it_pop(it, NULL);
                        return;
                }
        }

        rlc_assert(0);
}

struct rlc_sdu *rlc_sdu_alloc(struct rlc_context *ctx)
{
        struct rlc_sdu *sdu;

        sdu = rlc_alloc(ctx, sizeof(*sdu));
        if (sdu == NULL) {
                return NULL;
        }

        (void)memset(sdu, 0, sizeof(*sdu));

        sdu->refcount = 1;
        sdu->ctx = ctx;

        return sdu;
}

void rlc_sdu_incref(struct rlc_sdu *sdu)
{
        sdu->refcount++;
}

void rlc_sdu_decref(struct rlc_sdu *sdu)
{
        struct rlc_sdu_segment *seg;

        if (--sdu->refcount == 0) {
                gabs_pbuf_decref(sdu->buffer);

                for (rlc_each_node_safe(struct rlc_sdu_segment, sdu->segments,
                                        seg, next)) {
                        rlc_dealloc(sdu->ctx, seg);
                }

                rlc_dealloc(sdu->ctx, sdu);
        }
}

void rlc_sdu_queue_clear(rlc_sdu_queue *q)
{
        rlc_list_it it;
        struct rlc_sdu *sdu;

        rlc_list_foreach(q, it)
        {
                sdu = rlc_sdu_from_it(it);
                it = rlc_list_it_pop(it, NULL);

                rlc_sdu_decref(sdu);
        }
}

struct rlc_sdu *rlc_sdu_get(rlc_sdu_queue *q, uint32_t sn)
{
        struct rlc_sdu *sdu;
        rlc_list_it it;

        rlc_list_foreach(q, it)
        {
                sdu = rlc_sdu_from_it(it);

                if (sdu->sn == sn) {
                        return sdu;
                }
        }

        return NULL;
}
