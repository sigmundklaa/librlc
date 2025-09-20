
#ifndef RLC_BUF_H__
#define RLC_BUF_H__

#include <rlc/utils.h>
#include <rlc/rlc.h>

RLC_BEGIN_DECL

static inline rlc_buf *rlc_buf_alloc(struct rlc_context *ctx, size_t size)
{
        return rlc_plat_buf_alloc(ctx, size);
}

static inline void rlc_buf_incref(rlc_buf *buf)
{
        rlc_plat_buf_incref(buf);
}

static inline void rlc_buf_decref(rlc_buf *buf, struct rlc_context *ctx)
{
        rlc_plat_buf_decref(buf, ctx);
}

static inline void rlc_buf_chain_at(rlc_buf *buf, rlc_buf *next, size_t offset)
{
        rlc_plat_buf_chain_at(buf, next, offset);
}

static inline void rlc_buf_chain_back(rlc_buf *buf, rlc_buf *back)
{
        rlc_plat_buf_chain_back(buf, back);
}

static inline void rlc_buf_chain_front(rlc_buf *buf, rlc_buf *front)
{
        rlc_plat_buf_chain_front(buf, front);
}

static inline rlc_buf *rlc_buf_view(rlc_buf *buf, size_t offset, size_t size,
                                    struct rlc_context *ctx)
{
        return rlc_plat_buf_view(buf, offset, size, ctx);
}

static inline void rlc_buf_strip_front(rlc_buf *buf, size_t size)
{
        rlc_plat_buf_strip_front(buf, size);
}

static inline void rlc_buf_strip_back(rlc_buf *buf, size_t size)
{
        rlc_plat_buf_strip_back(buf, size);
}

static inline size_t rlc_buf_put_back(rlc_buf *buf, const void *mem,
                                      size_t size)
{
        return rlc_plat_buf_put_back(buf, mem, size);
}

static inline size_t rlc_buf_put_front(rlc_buf *buf, const void *mem,
                                       size_t size)
{
        return rlc_plat_buf_put_front(buf, mem, size);
}

static inline size_t rlc_buf_copy(const rlc_buf *buf, void *mem, size_t offset,
                                  size_t max_size)
{
        return rlc_plat_buf_copy(buf, mem, offset, max_size);
}

static inline size_t rlc_buf_cap(const rlc_buf *buf)
{
        return rlc_plat_buf_cap(buf);
}

static inline size_t rlc_buf_size(const rlc_buf *buf)
{
        return rlc_plat_buf_size(buf);
}

RLC_END_DECL

#endif /* RLC_BUF_H__ */
