
#ifndef RLC_BUF_H__
#define RLC_BUF_H__

#include <rlc/utils.h>
#include <rlc/rlc.h>

RLC_BEGIN_DECL

struct rlc_buf;

typedef void (*rlc_buf_dealloc_fn)(struct rlc_context *, struct rlc_buf *);

struct rlc_buf {
        void *mem;
        size_t size;

        unsigned int refcnt;
        void (*dealloc)(struct rlc_context *ctx, struct rlc_buf *buf);
};

void rlc_buf_init(struct rlc_buf *buf, void *mem, size_t size,
                  rlc_buf_dealloc_fn dealloc);

struct rlc_buf *rlc_buf_alloc(struct rlc_context *ctx, size_t size);

void rlc_buf_incref(struct rlc_buf *buf);
void rlc_buf_decref(struct rlc_buf *buf, struct rlc_context *ctx);

RLC_END_DECL

#endif /* RLC_BUF_H__ */
