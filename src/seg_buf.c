
#include <errno.h>

#include <rlc/seg_buf.h>
#include <rlc/utils.h>
#include <rlc/errno.h>
#include <rlc/seg_list.h>
#include <rlc/seg_buf.h>

static size_t seg_byte_offset(struct rlc_seg_buf *seg_buf, size_t start)
{
        struct rlc_seg_item *cur;
        size_t ret;
        rlc_list_it it;

        ret = 0;

        rlc_list_foreach(&seg_buf->segments, it)
        {
                cur = rlc_seg_item_from_it(it);

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

rlc_errno rlc_seg_buf_insert(struct rlc_seg_buf *seg_buf, gabs_pbuf *buf,
                             struct rlc_seg seg,
                             const gabs_allocator_h *alloc_misc,
                             const gabs_allocator_h *alloc_buf)
{
        struct rlc_seg unique;
        struct rlc_seg cur;
        gabs_pbuf insertbuf;
        size_t offset;
        rlc_errno status;

        status = 0;

        do {
                cur = seg;

                status = rlc_seg_list_insert(&seg_buf->segments, &seg, &unique,
                                             alloc_misc);
                if (status != 0) {
                        if (status == -ENODATA) {
                                status = 0;
                        }

                        break;
                }

                /* No remaining parts of the buffer that need to be inserted, so
                 * we don't need to create a new buffer. This is the most likely
                 * case. */
                if (!rlc_seg_okay(&seg)) {
                        gabs_pbuf_incref(*buf);

                        if (unique.start != cur.start) {
                                gabs_pbuf_strip_head(buf,
                                                     unique.start - cur.start);
                        }

                        if (unique.end != cur.end) {
                                gabs_pbuf_strip_tail(buf, cur.end - unique.end);
                        }

                        insertbuf = *buf;
                } else {
                        offset = unique.start - cur.start;
                        insertbuf = gabs_pbuf_clone(
                                *buf, offset,
                                offset + (unique.end - unique.start),
                                alloc_buf);

                        /* Strip off the bytes that are now handled by the new
                         * buffer, in addition to the bytes that are already
                         * inserted (which is the difference between the start
                         * of the remaining and the end of the unique). */
                        gabs_pbuf_strip_head(
                                buf, offset + gabs_pbuf_size(insertbuf) +
                                             (seg.start - unique.end));
                }

                gabs_pbuf_chain_at(&seg_buf->buf, insertbuf,
                                   seg_byte_offset(seg_buf, unique.start));
        } while (rlc_seg_okay(&seg));

        return status;
}

void rlc_seg_buf_destroy(struct rlc_seg_buf *buf, const gabs_allocator_h *alloc)
{
        (void)gabs_pbuf_decref(buf->buf);
        rlc_seg_list_clear(&buf->segments, alloc);
}
