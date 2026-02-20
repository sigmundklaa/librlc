
#ifndef RLC_SEG_BUF_H__
#define RLC_SEG_BUF_H__

#include <gabs/pbuf.h>

#include <rlc/decl.h>
#include <rlc/seg_list.h>

RLC_BEGIN_DECL

struct rlc_seg_buf {
        gabs_pbuf buf;
        rlc_seg_list segments;
};

/**
 * @brief Insert the contents of @p buf with offset specified in @p seg into
 * the segment buffer, removing any duplicate bytes. */
rlc_errno rlc_seg_buf_insert(struct rlc_seg_buf *seg_buf, gabs_pbuf *buf,
                             struct rlc_seg seg,
                             const gabs_allocator_h *alloc_misc,
                             const gabs_allocator_h *alloc_buf);

/**
 * @brief Destroy @p buf
 *
 * @param buf
 * @param alloc Allocator used to allocate `struct rlc_seg_item`.
 */
void rlc_seg_buf_destroy(struct rlc_seg_buf *buf,
                         const gabs_allocator_h *alloc);

RLC_END_DECL

#endif /* RLC_SEG_BUF_H__ */
