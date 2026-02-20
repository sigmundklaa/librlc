

#include <errno.h>

#include <gabs/alloc.h>

#include <rlc/seg_list.h>
#include <rlc/utils.h>

/** @brief Check if the start of @p right lies within the range of @p left */
static bool seg_overlap(const struct rlc_seg *left, const struct rlc_seg *right)
{
        return right->start >= left->start && right->start <= left->end;
}

rlc_errno rlc_seg_list_insert(rlc_seg_list *list, struct rlc_seg *segptr,
                              struct rlc_seg *unique,
                              const gabs_allocator_h *allocator)
{
        struct rlc_seg_item *slot;
        struct rlc_seg_item *left;
        struct rlc_seg_item *right;
        struct rlc_seg_item **lastp;
        struct rlc_seg seg;
        rlc_list_it it;
        bool overlap_left;
        bool overlap_right;

        left = NULL;
        right = NULL;

        seg = *segptr;

        rlc_list_foreach(list, it)
        {
                left = rlc_seg_item_from_it(it);
                right = rlc_seg_item_from_it(rlc_list_it_next(it));

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
        }

        /* Error check. This happens if the segment is completely contained
         * within the left or right neighbours and the segment should
         * then be completely discarded. */
        if (seg.start >= seg.end) {
                *segptr = (struct rlc_seg){0};
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
                        it = rlc_list_it_next(it);
                        rlc_assert(!rlc_list_it_eoi(it));

                        it = rlc_list_it_pop(it, NULL);
                        (void)gabs_dealloc(allocator, slot);
                }

                slot = left;
        } else if (slot == NULL) {
                if (gabs_alloc(allocator, sizeof(*slot), (void **)&slot) != 0) {
                        rlc_assert(0);
                        return -ENOMEM;
                }

                slot->seg = seg;
                it = rlc_list_it_put_back(it, &slot->list_node);
        }

        segptr->start = slot->seg.end;
        if (segptr->start >= segptr->end) {
                segptr->start = 0;
                segptr->end = 0;
        }

        return 0;
}

rlc_errno rlc_seg_list_insert_all(rlc_seg_list *list, struct rlc_seg seg,
                                  const gabs_allocator_h *allocator)
{
        rlc_errno status;
        struct rlc_seg unique;

        do {
                status = rlc_seg_list_insert(list, &seg, &unique, allocator);
        } while (rlc_seg_okay(&unique) && rlc_seg_okay(&seg));

        return status;
}

void rlc_seg_list_clear_until_last(rlc_seg_list *list,
                                   const gabs_allocator_h *allocator)
{
        rlc_list_it it;
        struct rlc_seg_item *item;

        it = rlc_list_it_init(list);

        for (;;) {
                if (rlc_list_it_eoi(rlc_list_it_next(it))) {
                        break;
                }

                item = rlc_seg_item_from_it(it);
                it = rlc_list_it_pop(it, NULL);
                (void)gabs_dealloc(allocator, item);
        }
}

void rlc_seg_list_clear(rlc_seg_list *list, const gabs_allocator_h *allocator)
{
        rlc_list_it it;
        struct rlc_seg_item *item;

        rlc_list_foreach(list, it)
        {
                item = rlc_seg_item_from_it(it);
                it = rlc_list_it_pop(it, NULL);

                (void)gabs_dealloc(allocator, item);
        }
}
