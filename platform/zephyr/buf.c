
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
        size_t bytes;

        bytes = 0;
        cur = buf;

        while (cur != NULL && bytes < offset) {
                bytes += cur->len;
                cur = cur->frags;
        }

        if (cur == NULL) {
                __ASSERT(0, "Buffer length < offset");
                return buf;
        }

        __ASSERT(bytes == offset, "Offset not aligned");

        if (cur->frags != NULL) {
                (void)net_buf_frag_add(next, cur->frags);
        }

        cur->frags = next;
        return buf;
}

rlc_buf *rlc_plat_buf_chain_back(rlc_buf *buf, rlc_buf *back)
{
        return net_buf_frag_add(buf, back);
}

rlc_buf *rlc_plat_buf_chain_front(rlc_buf *buf, rlc_buf *front)
{
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

        chunklen = rlc_min(parent->len, max_size) - offset;

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
                parent = parent->frags;
                bytes += parent->len;
        }

        if (parent == NULL) {
                __ASSERT_NO_MSG(0);
                return NULL;
        }

        head = create_view(parent, bytes - offset, size, ctx);
        remaining = size - head->len;

        while (remaining > 0) {
                cur = create_view(parent, 0, remaining, ctx);
                remaining -= cur->len;

                (void)net_buf_frag_add(net_buf_frag_last(head), cur);

                parent = parent->frags;
        }

        return head;
}

rlc_buf *rlc_plat_buf_strip_front(rlc_buf *buf, size_t size,
                                  struct rlc_context *ctx)
{
        return net_buf_skip(buf, size);
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

        if (bytes == offset) {
                (void)net_buf_remove_mem(cur, cur->len - (bytes - offset));
        }

        if (cur->frags != NULL) {
                net_buf_unref(cur->frags);
                cur->frags = NULL;
        }

        return cur;
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
