
#ifndef RLC_BUF_H__
#define RLC_BUF_H__

#include <rlc/rlc.h>
#include <rlc/decl.h>

RLC_BEGIN_DECL

/** @brief Allocate buffer with data size of @p size. */
static inline rlc_buf *rlc_buf_alloc(struct rlc_context *ctx, size_t size)
{
        return rlc_plat_buf_alloc(ctx, size);
}

/** @brief Increase buffer reference counter */
static inline void rlc_buf_incref(rlc_buf *buf)
{
        rlc_plat_buf_incref(buf);
}

/** @brief Decerease buffer reference counter, deallocating if reaching 0 */
static inline void rlc_buf_decref(rlc_buf *buf, struct rlc_context *ctx)
{
        rlc_plat_buf_decref(buf, ctx);
}

/**
 * @brief Add @p next to chain of buffers, at a byte offset of @p offset.
 *
 * @note Assumes that the buffers are aligned at @p offset. Passing an @p offset
 * that is not zero bytes into a buffer is undefined behaviour.
 *
 * @return rlc_buf* Head of chain
 */
static inline rlc_buf *rlc_buf_chain_at(rlc_buf *buf, rlc_buf *next,
                                        size_t offset)
{
        return rlc_plat_buf_chain_at(buf, next, offset);
}

/**
 * @brief Add @p back to the back of the chain poined to by @p buf
 *
 * @return rlc_buf* Head of chain
 */
static inline rlc_buf *rlc_buf_chain_back(rlc_buf *buf, rlc_buf *back)
{
        return rlc_plat_buf_chain_back(buf, back);
}

/**
 * @brief Add @p front to front of buffer chain.
 *
 * @return rlc_buf* Head of buffer chain
 */
static inline rlc_buf *rlc_buf_chain_front(rlc_buf *buf, rlc_buf *front)
{
        return rlc_plat_buf_chain_front(buf, front);
}

/**
 * @brief Create a readonly view around @p buf, offset by @p offset bytes into
 * the buffer, with a size of @p size.
 *
 * @return rlc_buf*
 */
static inline rlc_buf *rlc_buf_view(rlc_buf *buf, size_t offset, size_t size,
                                    struct rlc_context *ctx)
{
        return rlc_plat_buf_view(buf, offset, size, ctx);
}

/**
 * @brief Create a deep copy of @p buf, offset by @p offset bytes with a size
 * of @p size.
 *
 * @return rlc_buf*
 */
static inline rlc_buf *rlc_buf_clone(const rlc_buf *buf, size_t offset,
                                     size_t size, struct rlc_context *ctx)
{
        return rlc_plat_buf_clone(buf, offset, size, ctx);
}

/**
 * @brief Strip off @p size bytes from the start of @p buf.
 *
 * This may deallocate the head of the buffer, and return a new head. The
 * buffer specified in @p buf is then invalid after this call.
 *
 * @return rlc_buf*
 */
static inline rlc_buf *rlc_buf_strip_front(rlc_buf *buf, size_t size,
                                           struct rlc_context *ctx)
{
        return rlc_plat_buf_strip_front(buf, size, ctx);
}

/**
 * @brief Strip off @p size bytes from the end of @p buf.
 *
 * This may deallocate the tail of the buffer, and may return a new head. The
 * buffer specified in @p buf is then invalid after this call.
 *
 * @return rlc_buf*
 */
static inline rlc_buf *rlc_buf_strip_back(rlc_buf *buf, size_t size,
                                          struct rlc_context *ctx)
{
        return rlc_plat_buf_strip_back(buf, size, ctx);
}

/**
 * @brief Put @p size from @p mem into the back of the buffer of @p buf.
 *
 * @note This only appends data to the specific buffer given in @p buf, not
 * at the end of the buffer chain.
 */
static inline void rlc_buf_put(rlc_buf *buf, const void *mem, size_t size)
{
        rlc_plat_buf_put(buf, mem, size);
}

/**
 * @brief Copy @p max_size from an offset of @p offset bytes into @p buf into
 * @p mem.
 *
 * @return size_t Number of copied bytes
 */
static inline size_t rlc_buf_copy(const rlc_buf *buf, void *mem, size_t offset,
                                  size_t max_size)
{
        return rlc_plat_buf_copy(buf, mem, offset, max_size);
}

/** @brief Return the maximum capacity of the given buffer (not including the
 * rest of the buffer chain). */
static inline size_t rlc_buf_cap(const rlc_buf *buf)
{
        return rlc_plat_buf_cap(buf);
}

/** @brief Return the total size of the buffer chain */
static inline size_t rlc_buf_size(const rlc_buf *buf)
{
        return rlc_plat_buf_size(buf);
}

RLC_END_DECL

#endif /* RLC_BUF_H__ */
