
#include <string.h>

#include <rlc/record.h>
#include <rlc/buf.h>

#include "utils.h"

static rlc_buf_ci buf_balance(rlc_buf_ci buf_it, rlc_buf_ci nxt_it,
                              size_t min_sz)
{
        size_t tail_sz;
        size_t head_sz;
        size_t nxt_sz;
        size_t cur_sz;
        size_t copy_sz;

        cur_sz = rlc_buf_ci_size(buf_it);
        nxt_sz = rlc_buf_ci_size(nxt_it);
        tail_sz = rlc_buf_ci_tailroom(buf_it);
        head_sz = rlc_buf_ci_headroom(nxt_it);

        /* All of next can fit in current - move over */
        if (nxt_sz <= tail_sz) {
                (void)memcpy(rlc_buf_ci_reserve_tail(buf_it, nxt_sz),
                             rlc_buf_ci_release_head(nxt_it, nxt_sz), nxt_sz);

                (void)rlc_buf_ci_remove(nxt_it);
                return buf_it;
        }

        /* All of current can fit in next - move over */
        if (cur_sz <= head_sz) {
                (void)memcpy(rlc_buf_ci_reserve_head(nxt_it, cur_sz),
                             rlc_buf_ci_release_tail(buf_it, cur_sz), cur_sz);

                return rlc_buf_ci_remove(buf_it);
        }

        /* Not enough to split the two. TODO: Should maybe move over to head
         * anyways, to allow for future buffers to merge with the next? */
        if (cur_sz + nxt_sz < min_sz * 2) {
                return nxt_it;
        }

        if (cur_sz < min_sz) {
                copy_sz = min_sz - cur_sz;
                (void)memcpy(rlc_buf_ci_reserve_tail(buf_it, copy_sz),
                             rlc_buf_ci_release_head(nxt_it, cur_sz), cur_sz);

                return nxt_it;
        }

        if (nxt_sz < min_sz) {
                copy_sz = min_sz - nxt_sz;
                (void)memcpy(rlc_buf_ci_reserve_head(nxt_it, cur_sz),
                             rlc_buf_ci_release_tail(buf_it, copy_sz), cur_sz);

                return nxt_it;
        }

        return nxt_it;
}

void rlc_buf_defrag(rlc_buf *buf, const rlc_list *records, size_t min_size)
{
        struct rlc_record_node *recptr;
        struct rlc_record rec;
        struct rlc_buf *nextbuf;
        struct rlc_buf *curbuf;
        size_t cur_size;
        size_t nxt_size;
        size_t tot_size;
        size_t rec_size;
        size_t max_cap;
        rlc_buf_ci buf_it;
        rlc_buf_ci nxt_it;

        buf_it = rlc_buf_ci_init(buf);

        for (rlc_each_list(records, recptr, node)) {
                rec = recptr->rec;

                while (rec.start < rec.end) {
                        rec_size = rec.end - rec.start;

                        nxt_it = rlc_buf_ci_next(buf_it);
                        if (rlc_buf_ci_eoi(nxt_it)) {
                                return;
                        }

                        cur_size = rlc_buf_ci_size(buf_it);
                        nxt_size = rlc_buf_ci_size(nxt_it);

                        /* ? */
                        if (cur_size + nxt_size >= rec_size) {
                                break;
                        }

                        if (cur_size >= min_size && nxt_size >= min_size) {
                                continue;
                        }

                        buf_it = buf_balance(buf_it, nxt_it, min_size);
                }
        }
}

size_t rlc_buf_size(rlc_buf buf)
{
        rlc_buf_ci it;
        size_t size;

        size = 0;

        for (rlc_each_buf_ci(&buf, it)) {
                size += rlc_buf_ci_size(it);
        }

        return size;
}

void rlc_buf_chain_at(rlc_buf *buf, rlc_buf next, size_t offset)
{
        rlc_buf_ci it;
        rlc_buf_ci last;
        size_t bytes;

        bytes = 0;

        if (offset == 0) {
                rlc_buf_chain_front(buf, next);
                return;
        }

        for (rlc_each_buf_ci(buf, it)) {
                if (bytes + rlc_buf_ci_size(it) > offset) {
                        break;
                }

                bytes += rlc_buf_ci_size(it);
        }

        rlc_assert(bytes == offset);

        (void)rlc_buf_ci_insert(it, next);
}

void rlc_buf_chain_back(rlc_buf *buf, rlc_buf back)
{
        rlc_buf_ci it;

        for (rlc_each_buf_ci(buf, it)) {
        }

        (void)rlc_buf_ci_insert(it, back);
}

void rlc_buf_chain_front(rlc_buf *buf, rlc_buf front)
{
        rlc_buf_ci it;

        it = rlc_buf_ci_init(buf);

        (void)rlc_buf_ci_insert(it, front);
}

static rlc_buf_ci buf_seek(rlc_buf *buf, size_t offset, size_t *boundary)
{
        size_t bytes;
        rlc_buf_ci it;

        bytes = 0;

        for (rlc_each_buf_ci(buf, it)) {
                if (bytes + rlc_buf_ci_size(it) > offset) {
                        break;
                }

                bytes += rlc_buf_ci_size(it);
        }

        if (boundary != NULL) {
                *boundary = bytes;
        }

        return it;
}

rlc_buf rlc_buf_view(rlc_buf buf, size_t offset, size_t size,
                     struct rlc_context *ctx)
{
        rlc_buf_ci it;
        rlc_buf_ci inserter;
        size_t boundary;
        size_t cur_offset;
        size_t cur_size;
        rlc_buf view;

        it = buf_seek(&buf, offset, &boundary);

        cur_offset = offset - boundary;
        cur_size = rlc_buf_ci_size(it) - cur_offset;

        view = rlc_buf_alloc_ro(ctx, rlc_buf_ci_data(it) + cur_offset,
                                cur_size);
        if (!rlc_buf_okay(view)) {
                return view;
        }

        inserter = rlc_buf_ci_init(&view);
        size -= cur_size;

        while (size > 0) {
                it = rlc_buf_ci_next(it);
                cur_size = rlc_min(rlc_buf_ci_size(it), size);
                size -= cur_size;

                view = rlc_buf_alloc_ro(ctx, rlc_buf_ci_data(it), cur_size);
                if (!rlc_buf_okay(view)) {
                        rlc_errf("Unable to alloc RO buffer");
                        rlc_assert(0);

                        break;
                }

                inserter = rlc_buf_ci_insert(rlc_buf_ci_next(inserter), view);
        }

        return view;
}

size_t rlc_buf_copy(rlc_buf buf, uint8_t *mem, size_t offset, size_t max_size)
{
        rlc_buf_ci it;
        size_t boundary;
        size_t cur_offset;
        size_t cur_size;
        size_t size;

        it = buf_seek(&buf, offset, &boundary);

        cur_offset = offset - boundary;
        cur_size = rlc_min(rlc_buf_ci_size(it) - cur_offset, max_size);

        (void)memcpy(mem, rlc_buf_ci_data(it) + cur_offset, cur_size);

        mem += cur_size;
        size += cur_size;

        while (size < max_size && !rlc_buf_ci_eoi(it)) {
                it = rlc_buf_ci_next(it);
                cur_size = rlc_min(rlc_buf_ci_size(it), max_size - size);

                (void)memcpy(mem, rlc_buf_ci_data(it) + cur_offset, cur_size);

                mem += cur_size;
                size += cur_size;
        }

        return size;
}

rlc_buf rlc_buf_clone(rlc_buf buf, size_t offset, size_t size,
                      struct rlc_context *ctx)
{
        size_t bytes;
        rlc_buf ret;

        ret = rlc_buf_alloc(ctx, size);
        if (!rlc_buf_okay(ret)) {
                return ret;
        }

        bytes = rlc_buf_copy(
                buf, rlc_buf_ci_reserve_tail(rlc_buf_ci_init(&ret), size),
                offset, size);
        rlc_assert(bytes == size);

        return ret;
}

void rlc_buf_shrink(rlc_buf *buf, size_t offset, size_t size)
{
        rlc_buf_ci it;
        size_t bytes;
        size_t cursize;
        size_t rem;

        it = rlc_buf_ci_init(buf);

        if (offset > 0) {
                bytes = 0;

                while (!rlc_buf_ci_eoi(it)) {
                        if (bytes + rlc_buf_ci_size(it) > offset) {
                                it = rlc_buf_ci_next(it);

                                break;
                        }

                        bytes += rlc_buf_ci_size(it);
                        it = rlc_buf_ci_remove(it);
                }

                if (rlc_buf_ci_eoi(it)) {
                        return;
                }

                (void)rlc_buf_ci_release_head(it, bytes - offset);
        }

        {
                bytes = 0;
                rem = size;

                for (; !rlc_buf_ci_eoi(it); it = rlc_buf_ci_next(it)) {
                        bytes = rlc_min(rlc_buf_ci_size(it), rem);
                        if (bytes == rem) {
                                break;
                        }

                        rem -= bytes;
                }

                if (!rlc_buf_ci_eoi(it)) {
                        if (rlc_buf_ci_size(it) > rem) {
                                (void)rlc_buf_ci_release_tail(it, rem);

                                it = rlc_buf_ci_next(it);
                        }

                        if (!rlc_buf_ci_eoi(it)) {
                                rlc_buf_ci_detach(it);
                        }
                }
        }
}
