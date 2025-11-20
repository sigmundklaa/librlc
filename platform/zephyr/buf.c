
#include <zephyr/net_buf.h>

#include <rlc/rlc.h>
#include <rlc/plat.h>

#include "methods.h"

rlc_buf rlc_plat_buf_alloc(struct rlc_context *ctx, size_t size)
{
        struct net_buf *buf;

        buf = rlc_alloc(ctx, size, RLC_ALLOC_BUF);
        if (buf == NULL) {
                rlc_panicf(ENOMEM, "Failed buffer allocation");
                return (struct rlc_buf){0};
        }

        return rlc_buf_from_zephyr(buf);
}

rlc_buf rlc_plat_buf_alloc_ro(struct rlc_context *ctx, uint8_t *data,
                              size_t size)
{
        struct rlc_buf buf;

        buf = rlc_plat_buf_alloc(ctx, 0);
        if (!rlc_plat_buf_okay(buf)) {
                rlc_assert(0);
                return (struct rlc_buf){0};
        }

        net_buf_simple_init_with_data(&buf.frags->b, data, size);
        buf.frags->flags = NET_BUF_EXTERNAL_DATA;

        return buf;
}

bool rlc_plat_buf_okay(rlc_buf buf)
{
        return buf.frags != NULL;
}

void rlc_plat_buf_incref(rlc_buf buf)
{
        buf.frags = net_buf_ref(buf.frags);
}

void rlc_plat_buf_decref(rlc_buf buf)
{
        if (buf.frags != NULL) {
                net_buf_unref(buf.frags);
        }
}

struct rlc_buf_ci rlc_plat_buf_ci_init(rlc_buf *buf)
{
        return (struct rlc_buf_ci){
                .cur = buf->frags,
                .prev = NULL,
                .head = &buf->frags,
        };
}

struct rlc_buf_ci rlc_plat_buf_ci_next(struct rlc_buf_ci it)
{
        return (struct rlc_buf_ci){
                .cur = it.cur->frags,
                .prev = it.cur,
                .head = it.head,
        };
}

bool rlc_plat_buf_ci_eoi(rlc_buf_ci it)
{
        return it.cur == NULL;
}

uint8_t *rlc_plat_buf_ci_data(struct rlc_buf_ci it)
{
        return it.cur->data;
}

uint8_t *rlc_plat_buf_ci_reserve_head(struct rlc_buf_ci it, size_t bytes)
{
        return net_buf_push(it.cur, bytes);
}

uint8_t *rlc_plat_buf_ci_release_head(struct rlc_buf_ci it, size_t bytes)
{
        return net_buf_pull(it.cur, bytes);
}

uint8_t *rlc_plat_buf_ci_reserve_tail(struct rlc_buf_ci it, size_t bytes)
{
        return net_buf_add(it.cur, bytes);
}

uint8_t *rlc_plat_buf_ci_release_tail(struct rlc_buf_ci it, size_t bytes)
{
        return net_buf_remove_mem(it.cur, bytes);
}

size_t rlc_plat_buf_ci_headroom(struct rlc_buf_ci it)
{
        return net_buf_headroom(it.cur);
}

size_t rlc_plat_buf_ci_tailroom(struct rlc_buf_ci it)
{
        return net_buf_tailroom(it.cur);
}

struct rlc_buf_ci rlc_plat_buf_ci_insert(struct rlc_buf_ci it, rlc_buf buf)
{
        struct net_buf *cur;

        if (it.prev != NULL) {
                net_buf_frag_insert(it.prev, buf.frags);
                cur = buf.frags;
        } else {
                if (it.cur != NULL) {
                        cur = net_buf_frag_add(buf.frags, it.cur);
                } else {
                        cur = buf.frags;
                }

                *it.head = cur;
        }

        it.cur = buf.frags;
        return it;
}

void rlc_plat_buf_ci_detach(rlc_buf_ci it)
{
        struct net_buf *frag = it.cur;

        if (*it.head == it.cur) {
                *it.head = NULL;
        }

        while (frag != NULL) {
                frag = net_buf_frag_del(NULL, it.cur);
        }
}

struct rlc_buf_ci rlc_plat_buf_ci_remove(struct rlc_buf_ci it)
{
        bool is_head;

        is_head = *it.head == it.cur;
        it.cur = net_buf_frag_del(it.prev, it.cur);

        if (is_head) {
                *it.head = it.cur;
        }

        return it;
}

size_t rlc_plat_buf_ci_size(struct rlc_buf_ci it)
{
        return it.cur->len;
}
