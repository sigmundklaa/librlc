
#include <stdalign.h>

#include <rlc/buf.h>
#include <rlc/utils.h>

#include "methods.h"

static size_t aligned_(size_t sz, size_t align)
{
        return (sz + (align - 1)) & ~(align - 1);
}

static void ctx_dealloc_(struct rlc_context *ctx, struct rlc_buf *buf)
{
        size_t overhead;
        uintptr_t mem;

        overhead = aligned_(sizeof(*buf), alignof(*buf));
        mem = (uintptr_t)buf;

        /* Total memory was allocated with alignment requirements, and `buf`
         * was created by rounding up to the nearest aligned address. The
         * below undoes that, to obtain the address returned by the
         * allocation. */
        mem -= overhead;

        rlc_dealloc(ctx, (void *)mem);
}

void rlc_buf_init(struct rlc_buf *buf, void *mem, size_t size,
                  rlc_buf_dealloc_fn dealloc)
{
        buf->mem = mem;
        buf->size = size;
        buf->refcnt = 1;
        buf->dealloc = dealloc;
}

struct rlc_buf *rlc_buf_alloc(struct rlc_context *ctx, size_t size)
{
        size_t overhead;
        uintptr_t mem;
        struct rlc_buf *buf;

        overhead = aligned_(sizeof(*buf), alignof(*buf));
        mem = (uintptr_t)rlc_alloc(ctx, overhead + size);
        if (mem == 0) {
                rlc_panicf(ENOMEM, "rlc_buf_alloc");
                return NULL;
        }

        buf = (void *)aligned_(mem, alignof(*buf));

        rlc_buf_init(buf, (void *)(mem + overhead), size, ctx_dealloc_);

        return buf;
}

void rlc_buf_incref(struct rlc_buf *buf)
{
        buf->refcnt++;
}

void rlc_buf_decref(struct rlc_buf *buf, struct rlc_context *ctx)
{
        buf->refcnt--;

        if (buf->refcnt == 0 && buf->dealloc != NULL) {
                buf->dealloc(ctx, buf);
        }
}
