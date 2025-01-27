
#ifndef RLC_METHODS_H__
#define RLC_METHODS_H__

#include <errno.h>
#include <string.h>

#include <rlc/utils.h>
#include <rlc/rlc.h>

RLC_BEGIN_DECL

static inline rlc_errno rlc_tx_request(struct rlc_context *ctx)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->tx_request == NULL) {
                return -ENOTSUP;
        }

        return methods->tx_request(ctx);
}

static inline rlc_errno rlc_tx_submit(struct rlc_context *ctx,
                                      struct rlc_chunk *chunk)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->tx_submit == NULL) {
                return -ENOTSUP;
        }

        return methods->tx_submit(ctx, chunk);
}

static inline void *rlc_alloc(struct rlc_context *ctx, size_t size)
{
        const struct rlc_methods *methods = ctx->methods;
        void *mem;
        if (methods->mem_alloc == NULL) {
                return NULL;
        }

        mem = methods->mem_alloc(ctx, size);
        (void)memset(mem, 0, size);

        return mem;
}

static inline void rlc_dealloc(struct rlc_context *ctx, void *mem)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->mem_dealloc == NULL) {
                return;
        }

        methods->mem_dealloc(ctx, mem);
}

RLC_END_DECL

#endif /* RLC_METHODS_H__ */
