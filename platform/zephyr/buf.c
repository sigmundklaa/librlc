
#include <zephyr/net_buf.h>

#include <rlc/rlc.h>

#include "methods.h"

rlc_buf *rlc_plat_buf_alloc(struct rlc_context *ctx, size_t size)
{
        return rlc_alloc(ctx, size, RLC_ALLOC_BUF);
}

void rlc_plat_buf_incref(rlc_buf *buf)
{
        buf = net_buf_ref(buf);
}

void rlc_plat_buf_decref(rlc_buf *buf, struct rlc_context *ctx)
{
        (void)ctx;
        (void)net_buf_unref(buf);
}

size_t rlc_plat_buf_cap(const rlc_buf *buf)
{
        return net_buf_max_len(buf);
}

size_t rlc_plat_buf_size(const rlc_buf *buf)
{
        return net_buf_frags_len(buf);
}

rlc_buf *rlc_plat_buf_chain_at(rlc_buf *buf, rlc_buf *next, size_t offset)
{
        struct net_buf *cur;
        struct net_buf *last;
        size_t bytes;

        if (offset == 0) {
                return rlc_plat_buf_chain_front(buf, next);
        }

        bytes = 0;
        last = buf;

        for (cur = buf; cur != NULL && bytes < offset; cur = cur->frags) {
                if (bytes + cur->len > offset) {
                        break;
                }

                bytes += cur->len;
                last = cur;
        }

        if (last == NULL) {
                __ASSERT(0, "Buffer length < offset");
                return buf;
        }

        __ASSERT(bytes == offset, "Offset not aligned");

        if (last->frags != NULL) {
                (void)net_buf_frag_add(next, last->frags);
        }

        last->frags = next;
        return buf;
}

rlc_buf *rlc_plat_buf_chain_back(rlc_buf *buf, rlc_buf *back)
{
        return net_buf_frag_add(buf, back);
}

rlc_buf *rlc_plat_buf_chain_front(rlc_buf *buf, rlc_buf *front)
{
        if (buf == NULL) {
                return front;
        }

        return net_buf_frag_add(front, buf);
}

static rlc_buf *create_view(rlc_buf *parent, size_t offset, size_t max_size,
                            struct rlc_context *ctx)
{
        struct net_buf *buf;
        size_t chunklen;

        buf = rlc_plat_buf_alloc(ctx, 0);
        if (buf == NULL) {
                __ASSERT_NO_MSG(0);
                return NULL;
        }

        chunklen = rlc_min(parent->len - offset, max_size);

        net_buf_simple_init_with_data(&buf->b, parent->data + offset, chunklen);
        buf->flags = NET_BUF_EXTERNAL_DATA;

        return buf;
}

rlc_buf *rlc_plat_buf_view(rlc_buf *buf, size_t offset, size_t size,
                           struct rlc_context *ctx)
{
        struct net_buf *head;
        struct net_buf *cur;
        struct net_buf *parent;
        size_t bytes;
        size_t remaining;

        bytes = 0;
        parent = buf;

        while (parent != NULL && bytes + parent->len < offset) {
                bytes += parent->len;
                parent = parent->frags;
        }

        if (parent == NULL) {
                __ASSERT_NO_MSG(0);
                return NULL;
        }

        head = create_view(parent, offset - bytes, size, ctx);
        remaining = size - head->len;

        while (remaining > 0) {
                parent = parent->frags;
                __ASSERT_NO_MSG(parent != NULL);

                cur = create_view(parent, 0, remaining, ctx);
                remaining -= cur->len;

                (void)net_buf_frag_add(net_buf_frag_last(head), cur);
        }

        return head;
}

rlc_buf *rlc_plat_buf_clone(const rlc_buf *buf, size_t offset, size_t size,
                            struct rlc_context *ctx)
{
        size_t bytes;
        rlc_buf *ret;

        ret = rlc_plat_buf_alloc(ctx, size);
        if (ret == NULL) {
                return NULL;
        }

        bytes = rlc_plat_buf_copy(buf, ret->data, offset, ret->size);
        __ASSERT_NO_MSG(bytes == size);

        net_buf_add(ret, bytes);
        return ret;
}

/* Same as net_buf_skip, only that this does not remove the last element */
rlc_buf *rlc_plat_buf_strip_front(rlc_buf *buf, size_t size,
                                  struct rlc_context *ctx)
{
        size_t stripped;

        while (buf != NULL && size > 0) {
                stripped = rlc_min(size, buf->len);
                (void)net_buf_pull_mem(buf, stripped);

                size -= stripped;
                if (size == 0) {
                        break;
                }

                buf = net_buf_frag_del(NULL, buf);
        }

        __ASSERT_NO_MSG(buf != NULL);

        return buf;
}

rlc_buf *rlc_plat_buf_strip_back(rlc_buf *buf, size_t size,
                                 struct rlc_context *ctx)
{
        struct net_buf *cur;
        size_t total;
        size_t bytes;
        size_t offset;

        bytes = 0;
        total = net_buf_frags_len(buf);
        offset = total - size;

        for (cur = buf; cur && bytes + cur->len <= offset; cur = cur->frags) {
                bytes += cur->len;
        }

        rlc_assert(cur != NULL);

        (void)net_buf_remove_mem(cur, cur->len - (offset - bytes));

        if (cur->frags != NULL) {
                net_buf_unref(cur->frags);
                cur->frags = NULL;
        }

        return buf;
}

size_t rlc_plat_buf_copy(const rlc_buf *buf, void *mem, size_t offset,
                         size_t max_size)
{
        return net_buf_linearize(mem, max_size, buf, offset, max_size);
}

void rlc_plat_buf_put(rlc_buf *buf, const void *mem, size_t size)
{
        (void)net_buf_add_mem(buf, mem, size);
}
