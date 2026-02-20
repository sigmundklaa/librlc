
#ifndef RLC_SEG_LIST_H__
#define RLC_SEG_LIST_H__

#include <gabs/alloc.h>

#include <rlc/decl.h>
#include <rlc/list.h>
#include <rlc/errno.h>

RLC_BEGIN_DECL

struct rlc_seg {
        uint32_t start;
        uint32_t end;
};

struct rlc_seg_item {
        struct rlc_seg seg;
        rlc_list_node list_node;
};

typedef rlc_list rlc_seg_list;

static inline bool rlc_seg_okay(struct rlc_seg *segment)
{
        return segment->start != 0 || segment->end != 0;
}

static inline struct rlc_seg_item *rlc_seg_item_from_it(rlc_list_it it)
{
        return rlc_list_it_item(it, struct rlc_seg_item, list_node);
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
 * @param seg_buf
 * @param segptr Pointer to segment to insert. If the segment must be split into
 * seperate areas, this is updated with the remaining parts of the segment that
 * is not represented in the returned segment.
 * @param unique Pointer where adjusted segment will be stored.
 * @param allocator Allocator to allocate `struct seg_list_item` with
 * @return rlc_errno
 * @retval -ENODATA No unique data in @p seg
 * @retval -ENOMEM Unable to allocate memory for segment
 */
rlc_errno rlc_seg_list_insert(rlc_seg_list *list, struct rlc_seg *segptr,
                              struct rlc_seg *unique,
                              const gabs_allocator_h *allocator);

/**
 * @brief Insert segment into segment list, repeating however many times is
 * necessary to fully insert the segment.
 *
 * See @ref rlc_seg_buf_insert for further explanation.
 */
rlc_errno rlc_seg_list_insert_all(rlc_seg_list *list, struct rlc_seg seg,
                                  const gabs_allocator_h *allocator);

/**
 * @brief Clear all but last element in segment list.
 *
 * @param list
 * @param allocator Allocator used to allocate `struct rlc_seg_item`
 */
void rlc_seg_list_clear_until_last(rlc_seg_list *list,
                                   const gabs_allocator_h *allocator);

/** @brief Clear (delete) all segments in the list */
void rlc_seg_list_clear(rlc_seg_list *list, const gabs_allocator_h *allocator);

RLC_END_DECL

#endif /* RLC_SEG_LIST_H__ */
