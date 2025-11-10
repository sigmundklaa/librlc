
#include <stdbool.h>
#include <errno.h>

#include <rlc/record.h>
#include <rlc/rlc.h>

#include "utils.h"
#include "methods.h"

rlc_errno rlc_record_insert_partial(rlc_list *list,
                                    const struct rlc_record *rec,
                                    struct rlc_record *remaining);

/** @brief Check if the start of @p right lies within the range of @p left */
static bool rec_overlap(const struct rlc_record *left,
                        const struct rlc_record *right)
{
        return right->start >= left->start && right->start <= left->end;
}

rlc_errno rlc_record_insert(rlc_list *list, struct rlc_record *recptr,
                            struct rlc_record *unique, struct rlc_context *ctx)
{
        struct rlc_record_node *slot;
        struct rlc_record_node *left;
        struct rlc_record_node *right;
        struct rlc_list_node **lastp;
        struct rlc_record rec;
        bool overlap_left;
        bool overlap_right;

        lastp = &rlc_list_head_node(list);
        left = NULL;
        right = NULL;

        rec = *recptr;

        for (rlc_each_list(list, left, node)) {
                right = rlc_list_next(left, node);

                overlap_left = rec_overlap(&left->rec, &rec);
                overlap_right = right && rec_overlap(&rec, &right->rec);

                /* Overlapping with either the left or right record. In that
                 * case, we need to insert atleast a chunk of the record now.
                 *
                 * NOTE: This moves the start of the record if overlapping to
                 * the left, and the end of the record if overlapping to the
                 * right. If the record is completely contained within
                 * either left or right, this will cause rec.start>=rec.end.
                 * This case should be (and is) handled before returning.
                 */
                if (overlap_left || overlap_right) {
                        if (overlap_left) {
                                rec.start = left->rec.end;
                        }

                        if (overlap_right) {
                                rec.end = right->rec.start;
                        }

                        lastp = &rlc_list_next_node(left, node);
                        break;
                }

                /* Not overlapping, but should be inserted before the left
                 * neighbour, meaning we can't continue iterating from here. */
                if (rec.start <= left->rec.start) {
                        rec.end = rlc_min(left->rec.start, rec.end);

                        right = left;
                        left = NULL;

                        break;
                }

                lastp = &rlc_list_next_node(left, node);
        }

        /* Error check. This happens if the segment is completely contained
         * within the left or right neighbours and the segment should
         * then be completely discarded. */
        if (rec.start >= rec.end) {
                *recptr = (struct rlc_record){0};
                *unique = *recptr;

                return -ENODATA;
        }

        *unique = rec;
        slot = NULL;

        /* New segment and the right neighbour can be merged together. */
        if (right != NULL && rec.end >= right->rec.start) {
                rec.end = right->rec.end;
                right->rec = rec;

                slot = right;
        }

        /* New segment can be merged into left one */
        if (left != NULL && rec.start <= left->rec.end) {
                left->rec.end = rec.end;

                /* Already merged with right, so we are now merging with two,
                 * meaning one can be deleted. In this case, we delete the right
                 * one */
                if (slot != NULL) {
                        rlc_list_pop(lastp, slot, node);
                        rlc_dealloc(ctx, slot, RLC_ALLOC_RECORD);
                }

                slot = left;
        }

        if (slot == NULL) {
                slot = rlc_alloc(ctx, sizeof(*slot), RLC_ALLOC_RECORD);
                if (slot == NULL) {
                        rlc_assert(0);
                        return -ENOMEM;
                }

                slot->rec = rec;
                rlc_list_put(lastp, slot, node);
        }

        recptr->start = slot->rec.end;
        if (recptr->start >= recptr->end) {
                recptr->start = 0;
                recptr->end = 0;
        }

        return 0;
}

size_t rlc_record_byte_offset(const rlc_list *list, size_t start)
{
        const struct rlc_record_node *cur;
        size_t ret;

        ret = 0;

        for (rlc_each_list(list, cur, node)) {
                if (cur->rec.start >= start) {
                        break;
                } else if (cur->rec.end >= start) {
                        ret += start - cur->rec.start;
                        break;
                }

                ret += cur->rec.end - cur->rec.start;
        }

        return ret;
}
