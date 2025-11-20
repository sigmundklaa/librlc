
#include <string.h>

#include <rlc/buf.h>

#include "utils.h"

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
                if (offset == 0 || bytes + rlc_buf_ci_size(it) > offset) {
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
        cur_size = rlc_min(rlc_buf_ci_size(it) - cur_offset, size);

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

        size = 0;
        it = buf_seek(&buf, offset, &boundary);

        if (!rlc_buf_okay(buf)) {
                return 0;
        }

        cur_offset = offset - boundary;
        cur_size = rlc_min(rlc_buf_ci_size(it) - cur_offset, max_size);

        (void)memcpy(mem, rlc_buf_ci_data(it) + cur_offset, cur_size);

        mem += cur_size;
        size += cur_size;

        while (size < max_size) {
                it = rlc_buf_ci_next(it);
                if (rlc_buf_ci_eoi(it)) {
                        break;
                }

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
        size_t rem;

        it = rlc_buf_ci_init(buf);

        if (offset > 0) {
                bytes = 0;

                while (!rlc_buf_ci_eoi(it)) {
                        if (bytes + rlc_buf_ci_size(it) > offset) {
                                break;
                        }

                        bytes += rlc_buf_ci_size(it);
                        it = rlc_buf_ci_remove(it);
                }

                if (rlc_buf_ci_eoi(it)) {
                        return;
                }

                if (offset > bytes) {
                        (void)rlc_buf_ci_release_head(it, offset - bytes);
                }
        }

        {
                bytes = 0;
                rem = size;

                /* Iterate over `size` bytes, and remove anything after those
                 * bytes. */
                for (; rem && !rlc_buf_ci_eoi(it); it = rlc_buf_ci_next(it)) {
                        bytes = rlc_min(rlc_buf_ci_size(it), rem);
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

void rlc_buf_strip_head(rlc_buf *buf, size_t size)
{
        rlc_buf_shrink(buf, size, rlc_buf_size(*buf) - size);
}

void rlc_buf_strip_tail(rlc_buf *buf, size_t size)
{
        rlc_buf_shrink(buf, 0, rlc_buf_size(*buf) - size);
}

void rlc_buf_put(rlc_buf *buf, const uint8_t *data, size_t size)
{
        rlc_buf_ci it;
        size_t bytes;
        uint8_t *mem;

        it = rlc_buf_ci_init(buf);

        for (; size > 0 && !rlc_buf_ci_eoi(it); it = rlc_buf_ci_next(it)) {
                bytes = rlc_min(rlc_buf_ci_tailroom(it), size);

                mem = rlc_buf_ci_reserve_tail(it, bytes);
                rlc_assert(mem != NULL);

                (void)memcpy(mem, data, bytes);
                data += bytes;
                size -= bytes;
        }
}

size_t rlc_buf_tailroom(rlc_buf buf)
{
        rlc_buf_ci it;

        for (rlc_each_buf_ci(&buf, it)) {
                if (rlc_buf_ci_eoi(rlc_buf_ci_next(it))) {
                        break;
                }
        }

        if (rlc_buf_ci_eoi(it)) {
                return 0;
        }

        return rlc_buf_ci_tailroom(it);
}

size_t rlc_buf_headroom(rlc_buf buf)
{
        rlc_buf_ci it;

        it = rlc_buf_ci_init(&buf);
        if (rlc_buf_ci_eoi(it)) {
                return 0;
        }

        return rlc_buf_ci_headroom(it);
}
