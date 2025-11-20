
#ifndef RLC_BUF_H__
#define RLC_BUF_H__

#include <rlc/plat.h>
#include <rlc/rlc.h>
#include <rlc/decl.h>

RLC_BEGIN_DECL

#define rlc_each_buf_ci(buf_, it_)                                             \
        it_ = rlc_buf_ci_init(buf_);                                           \
        !rlc_buf_ci_eoi(it_);                                                  \
        it_ = rlc_buf_ci_next(it_)

static inline rlc_buf_ci rlc_buf_ci_init(rlc_buf *buf)
{
        return rlc_plat_buf_ci_init(buf);
}

static inline rlc_buf_ci rlc_buf_ci_next(rlc_buf_ci it)
{
        return rlc_plat_buf_ci_next(it);
}

static inline bool rlc_buf_ci_eoi(rlc_buf_ci it)
{
        return rlc_plat_buf_ci_eoi(it);
}

static inline uint8_t *rlc_buf_ci_data(rlc_buf_ci it)
{
        return rlc_plat_buf_ci_data(it);
}

static inline uint8_t *rlc_buf_ci_reserve_head(struct rlc_buf_ci it,
                                               size_t bytes)
{
        return rlc_plat_buf_ci_reserve_head(it, bytes);
}

static inline uint8_t *rlc_buf_ci_release_head(struct rlc_buf_ci it,
                                               size_t bytes)
{
        return rlc_plat_buf_ci_release_head(it, bytes);
}

static inline uint8_t *rlc_buf_ci_reserve_tail(struct rlc_buf_ci it,
                                               size_t bytes)
{
        return rlc_plat_buf_ci_reserve_tail(it, bytes);
}

static inline uint8_t *rlc_buf_ci_release_tail(struct rlc_buf_ci it,
                                               size_t bytes)
{
        return rlc_plat_buf_ci_release_tail(it, bytes);
}

static inline size_t rlc_buf_ci_headroom(struct rlc_buf_ci it)
{
        return rlc_plat_buf_ci_headroom(it);
}

static inline size_t rlc_buf_ci_tailroom(struct rlc_buf_ci it)

{
        return rlc_plat_buf_ci_tailroom(it);
}

static inline rlc_buf_ci rlc_buf_ci_insert(rlc_buf_ci it, rlc_buf buf)
{
        return rlc_plat_buf_ci_insert(it, buf);
}

static inline rlc_buf_ci rlc_buf_ci_remove(rlc_buf_ci it)
{
        return rlc_plat_buf_ci_remove(it);
}

static inline void rlc_buf_ci_detach(rlc_buf_ci it)
{
        return rlc_plat_buf_ci_detach(it);
}

static inline size_t rlc_buf_ci_cap(rlc_buf_ci it)
{
        return rlc_plat_buf_ci_cap(it);
}

static inline size_t rlc_buf_ci_size(rlc_buf_ci it)
{
        return rlc_plat_buf_ci_size(it);
}

/** @brief Allocate buffer with data size of @p size. */
static inline rlc_buf rlc_buf_alloc(struct rlc_context *ctx, size_t size)
{
        return rlc_plat_buf_alloc(ctx, size);
}

static inline rlc_buf rlc_buf_alloc_ro(struct rlc_context *ctx, uint8_t *data,
                                       size_t size)
{
        return rlc_plat_buf_alloc_ro(ctx, data, size);
}

static inline bool rlc_buf_okay(rlc_buf buf)
{
        return rlc_plat_buf_okay(buf);
}

/** @brief Increase buffer reference counter */
static inline void rlc_buf_incref(rlc_buf buf)
{
        rlc_plat_buf_incref(buf);
}

/** @brief Decerease buffer reference counter, deallocating if reaching 0 */
static inline void rlc_buf_decref(rlc_buf buf)
{
        rlc_plat_buf_decref(buf);
}

/**
 * @brief Add @p next to chain of buffers, at a byte offset of @p offset.
 *
 * @note Assumes that the buffers are aligned at @p offset. Passing an @p offset
 * that is not zero bytes into a buffer is undefined behaviour.
 */
void rlc_buf_chain_at(rlc_buf *buf, rlc_buf next, size_t offset);

/**
 * @brief Add @p back to the back of the chain poined to by @p buf
 */
void rlc_buf_chain_back(rlc_buf *buf, rlc_buf back);

/**
 * @brief Add @p front to front of buffer chain.
 */
void rlc_buf_chain_front(rlc_buf *buf, rlc_buf front);

/**
 * @brief Create a readonly view around @p buf, offset by @p offset bytes into
 * the buffer, with a size of @p size.
 *
 * @return rlc_buf*
 */
rlc_buf rlc_buf_view(rlc_buf buf, size_t offset, size_t size,
                     struct rlc_context *ctx);

/**
 * @brief Create a deep copy of @p buf, offset by @p offset bytes with a size
 * of @p size.
 *
 * @return rlc_buf*
 */
rlc_buf rlc_buf_clone(rlc_buf buf, size_t offset, size_t size,
                      struct rlc_context *ctx);

/**
 * @brief Put @p size from @p mem into the back of the buffer of @p buf.
 *
 * @note This only appends data to the specific buffer given in @p buf, not
 * at the end of the buffer chain.
 */
void rlc_buf_put(rlc_buf *buf, const uint8_t *mem, size_t size);

void rlc_buf_strip_head(rlc_buf *buf, size_t size);

void rlc_buf_strip_tail(rlc_buf *buf, size_t size);

/**
 * @brief Copy @p max_size from an offset of @p offset bytes into @p buf into
 * @p mem.
 *
 * @return size_t Number of copied bytes
 */
size_t rlc_buf_copy(rlc_buf buf, uint8_t *mem, size_t offset, size_t max_size);

/** @brief Return the total size of the buffer chain */
size_t rlc_buf_size(rlc_buf buf);

size_t rlc_buf_tailroom(rlc_buf buf);

size_t rlc_buf_headroom(rlc_buf buf);

RLC_END_DECL

#endif /* RLC_BUF_H__ */
