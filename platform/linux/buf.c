
#include <stdalign.h>

#include <rlc/buf.h>
#include <rlc/rlc.h>

#include "methods.h"
#include "utils.h"
#include "log.h"

rlc_buf rlc_plat_buf_alloc(struct rlc_context *ctx, size_t size)
{
        struct rlc_buf_frag *buf;

        buf = rlc_alloc(ctx, size, RLC_ALLOC_BUF);
        if (buf == NULL) {
                rlc_panicf(ENOMEM, "Failed buffer allocation");
                return (struct rlc_buf){0};
        }

        buf->cap = size;
        buf->size = 0;
        buf->data = (uint8_t *)buf + sizeof(*buf);
        buf->next = NULL;
        buf->refcnt = 1;
        buf->readonly = false;
        buf->ctx = ctx;

        return (struct rlc_buf){.frags = buf};
}

rlc_buf rlc_plat_buf_alloc_ro(struct rlc_context *ctx, uint8_t *data,
                              size_t size)
{
        struct rlc_buf buf;
        struct rlc_buf_frag *frag;

        buf = rlc_plat_buf_alloc(ctx, 0);
        if (!rlc_plat_buf_okay(buf)) {
                rlc_assert(0);
                return (struct rlc_buf){0};
        }

        frag = buf.frags;

        frag->readonly = true;
        frag->data = data;
        frag->size = size;
        frag->cap = size;
        frag->ctx = ctx;

        return buf;
}

bool rlc_plat_buf_okay(rlc_buf buf)
{
        return buf.frags != NULL;
}

void rlc_plat_buf_incref(rlc_buf buf)
{
        buf.frags->refcnt++;
}

void rlc_plat_buf_decref(rlc_buf buf)
{
        struct rlc_buf_frag *cur;
        struct rlc_buf_frag *next;

        for (cur = buf.frags; cur != NULL; cur = next) {
                next = cur->next;

                if (--cur->refcnt > 0) {
                        break;
                }

                rlc_dealloc(cur->ctx, cur, RLC_ALLOC_BUF);
        }
}

struct rlc_buf_ci rlc_plat_buf_ci_init(rlc_buf *buf)
{
        /* TODO: Last pointer */
        return (struct rlc_buf_ci){
                .cur = buf->frags,
                .lastp = &buf->frags,
        };
}

struct rlc_buf_ci rlc_plat_buf_ci_next(struct rlc_buf_ci it)
{
        return (struct rlc_buf_ci){
                .cur = it.cur->next,
                .lastp = &it.cur->next,
        };
}

bool rlc_plat_buf_ci_eoi(rlc_buf_ci it)
{
        return it.cur == NULL;
}

uint8_t *rlc_plat_buf_ci_data(struct rlc_buf_ci it)
{
        return (uint8_t *)(it.cur + 1);
}

uint8_t *rlc_plat_buf_ci_reserve_head(struct rlc_buf_ci it, size_t bytes)
{
        uint8_t *data;

        rlc_assert(bytes <= rlc_buf_ci_headroom(it));

        data = it.cur->data - bytes;
        it.cur->size += bytes;

        return data;
}

uint8_t *rlc_plat_buf_ci_release_head(struct rlc_buf_ci it, size_t bytes)
{
        uint8_t *data;

        rlc_assert(bytes <= it.cur->size);

        data = it.cur->data;
        it.cur->size -= bytes;
        it.cur->data += bytes;

        return data;
}

uint8_t *rlc_plat_buf_ci_reserve_tail(struct rlc_buf_ci it, size_t bytes)
{
        uint8_t *data;

        rlc_assert(bytes <= rlc_buf_ci_tailroom(it));

        data = it.cur->data + it.cur->size;
        it.cur->size += bytes;

        return data;
}

uint8_t *rlc_plat_buf_ci_release_tail(struct rlc_buf_ci it, size_t bytes)
{
        rlc_assert(bytes <= it.cur->size);

        it.cur->size -= bytes;

        return it.cur->data + it.cur->size;
}

size_t rlc_plat_buf_ci_headroom(struct rlc_buf_ci it)
{
        return it.cur->data - rlc_buf_ci_data(it);
}

size_t rlc_plat_buf_ci_tailroom(struct rlc_buf_ci it)
{
        return it.cur->cap - it.cur->size - rlc_plat_buf_ci_headroom(it);
}

struct rlc_buf_ci rlc_plat_buf_ci_insert(struct rlc_buf_ci it, rlc_buf buf)
{
        struct rlc_buf_frag **lastp;
        struct rlc_buf_frag *cur;

        lastp = &buf.frags;

        for (cur = buf.frags; cur != NULL; cur = cur->next) {
                lastp = &cur->next;
        }

        *lastp = it.cur;
        *it.lastp = buf.frags;

        return (struct rlc_buf_ci){
                .cur = buf.frags,
                .lastp = it.lastp,
        };
}

struct rlc_buf_ci rlc_plat_buf_ci_remove(struct rlc_buf_ci it)
{
        struct rlc_buf_frag *buf;

        buf = it.cur;
        it.cur = it.cur->next;
        *it.lastp = it.cur;

        rlc_plat_buf_decref((struct rlc_buf){buf});

        return it;
}

size_t rlc_plat_buf_ci_size(struct rlc_buf_ci it)
{
        return it.cur->size;
}
