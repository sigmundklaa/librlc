
#ifndef RLC_METHODS_H__
#define RLC_METHODS_H__

#include <errno.h>
#include <string.h>

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

static inline rlc_errno rlc_tx_submit(struct rlc_context *ctx, gabs_pbuf buf)
{
        const struct rlc_methods *methods = ctx->methods;
        if (methods->tx_submit == NULL) {
                return -ENOTSUP;
        }

        return methods->tx_submit(ctx, buf);
}

static inline void *rlc_alloc(struct rlc_context *ctx, size_t size)
{
        void *mem;
        if (gabs_alloc(ctx->alloc_misc, size, &mem) != 0) {
                return NULL;
        }

        return mem;
}

static inline void rlc_dealloc(struct rlc_context *ctx, void *mem)
{
        (void)gabs_dealloc(ctx->alloc_misc, mem);
}

RLC_END_DECL

#endif /* RLC_METHODS_H__ */
