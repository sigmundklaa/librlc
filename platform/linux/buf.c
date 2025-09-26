
#include <stdalign.h>

#include <rlc/buf.h>
#include <rlc/utils.h>
#include <rlc/rlc.h>

#include "methods.h"

rlc_buf *rlc_plat_buf_alloc(struct rlc_context *ctx, size_t size)
{
        rlc_buf *buf;

        buf = rlc_alloc(ctx, size, RLC_ALLOC_BUF);
        if (buf == NULL) {
                rlc_panicf(ENOMEM, "Failed buffer allocation");
                return NULL;
        }

        buf->cap = size;
        buf->size = size;
        buf->data = (uint8_t *)buf + sizeof(*buf);
        buf->next = NULL;
        buf->refcnt = 1;
        buf->readonly = false;

        return buf;
}

void rlc_plat_buf_incref(rlc_buf *buf)
{
        buf->refcnt++;
}

void rlc_plat_buf_decref(rlc_buf *buf, struct rlc_context *ctx)
{
        if (--buf->refcnt == 0) {
                rlc_dealloc(ctx, buf, RLC_ALLOC_BUF);
        }
}

size_t rlc_plat_buf_cap(const rlc_buf *buf)
{
        return buf->cap;
}

size_t rlc_plat_buf_size(const rlc_buf *buf)
{
        const struct rlc_buf *cur;
        size_t size;

        size = 0;

        for (rlc_each_node(buf, cur, next)) {
                size += buf->size;
        }

        return size;
}

rlc_buf *rlc_plat_buf_chain_at(rlc_buf *buf, rlc_buf *next, size_t offset)
{
        struct rlc_buf *cur;
        size_t bytes;

        bytes = 0;

        for (rlc_each_node(buf, cur, next)) {
                if (bytes >= offset) {
                        break;
                }
        }

        if (cur == NULL) {
                rlc_assert(0);
                return buf;
        }

        rlc_assert(bytes == offset);

        if (cur->next != NULL) {
                rlc_plat_buf_chain_back(next, cur->next);
        }

        cur->next = next;

        return buf;
}

rlc_buf *rlc_plat_buf_chain_back(rlc_buf *buf, rlc_buf *back)
{
        struct rlc_buf **lastp;
        struct rlc_buf *cur;

        lastp = NULL;

        for (rlc_each_node(buf, cur, next)) {
                lastp = &cur->next;
        }

        rlc_assert(lastp != NULL);

        *lastp = back;

        return buf;
}

rlc_buf *rlc_plat_buf_chain_front(rlc_buf *buf, rlc_buf *front)
{
        return rlc_plat_buf_chain_back(front, buf);
}

static rlc_buf *create_view(const rlc_buf *parent, size_t offset,
                            size_t max_size, struct rlc_context *ctx)
{
        struct rlc_buf *buf;
        size_t chunklen;

        buf = rlc_plat_buf_alloc(ctx, 0);
        if (buf == NULL) {
                rlc_assert(0);
                return NULL;
        }

        chunklen = rlc_min(parent->size, max_size) - offset;

        buf->readonly = true;
        buf->data = parent->data + offset;
        buf->size = chunklen;
        buf->cap = chunklen;

        return buf;
}

static const struct rlc_buf *buf_seek(const rlc_buf *buf, size_t offset,
                                      size_t *count)
{
        size_t bytes;
        const struct rlc_buf *cur;

        bytes = 0;

        for (rlc_each_node(buf, cur, next)) {
                if (bytes + cur->size > offset) {
                        break;
                }

                bytes += cur->size;
        }

        if (count != NULL) {
                *count = bytes;
        }

        rlc_assert(cur != NULL);
        return cur;
}

rlc_buf *rlc_plat_buf_view(rlc_buf *buf, size_t offset, size_t size,
                           struct rlc_context *ctx)
{
        struct rlc_buf *head;
        struct rlc_buf *cur;
        struct rlc_buf *last;
        const struct rlc_buf *parent;
        size_t bytes;
        size_t remaining;

        parent = buf_seek(buf, offset, &bytes);

        head = create_view(parent, bytes - offset, size, ctx);
        remaining = size - head->size;
        last = head;

        while (remaining > 0) {
                cur = create_view(parent, 0, remaining, ctx);
                remaining -= cur->size;

                last->next = cur;

                last = cur;
                parent = parent->next;
        }

        return head;
}

rlc_buf *rlc_plat_buf_strip_front(rlc_buf *buf, size_t size,
                                  struct rlc_context *ctx)
{
        struct rlc_buf *cur;
        struct rlc_buf *head;
        size_t remaining;
        size_t stripped;

        rlc_assert(size != 0);

        remaining = size;

        for (rlc_each_node_safe(struct rlc_buf, buf, cur, next)) {
                stripped = rlc_min(remaining, cur->size);
                cur->data += stripped;
                cur->size -= stripped;

                if (remaining == 0) {
                        break;
                }

                head = cur->next;
                rlc_plat_buf_decref(cur, ctx);
        }

        return head;
}

rlc_buf *rlc_plat_buf_strip_back(rlc_buf *buf, size_t size,
                                 struct rlc_context *ctx)
{
        size_t total;
        size_t bytes;
        size_t offset;
        struct rlc_buf *cur;
        struct rlc_buf *del;

        total = rlc_buf_size(buf);
        rlc_assert(total >= size);

        offset = total - size;

        cur = (rlc_buf *)buf_seek(buf, offset, &bytes);
        cur->size = bytes - offset;

        for (rlc_each_node_safe(struct rlc_buf, cur->next, del, next)) {
                rlc_plat_buf_decref(cur, ctx);
        }

        cur->next = NULL;
        return buf;
}

size_t rlc_plat_buf_copy(const rlc_buf *buf, void *mem, size_t offset,
                         size_t max_size)
{
        const struct rlc_buf *cur;
        size_t bytes;
        size_t copy_size;
        size_t cur_offset;
        uint8_t *data;
        size_t remaining;

        data = mem;
        remaining = max_size;

        cur = buf_seek(buf, offset, &bytes);
        cur_offset = bytes - offset;

        do {
                copy_size = rlc_min(remaining, cur->size - cur_offset);
                (void)memcpy(data, cur->data + cur_offset, copy_size);

                cur_offset = 0;

                remaining -= copy_size;
                bytes += copy_size;
                data += copy_size;

                cur = cur->next;
        } while (cur != NULL && remaining > 0);

        return bytes;
}

void rlc_plat_buf_put(rlc_buf *buf, const void *mem, size_t size)
{
        rlc_assert(size <= buf->cap);

        (void)memcpy(buf->data, mem, size);
        buf->size = size;
}
