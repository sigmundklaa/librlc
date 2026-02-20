
#ifndef RLC_COMMON_H__
#define RLC_COMMON_H__

#include <rlc/rlc.h>

static inline void rlc_lock_acquire(gabs_mutex *lock)
{
        int status;

        status = gabs_mutex_lock(lock, GABS_TIMEOUT_MAX);
        if (status != 0) {
                rlc_panicf(status, "Unable to acquire lock");
        }
}

static inline void rlc_lock_release(gabs_mutex *lock)
{
        int status;

        status = gabs_mutex_unlock(lock);
        if (status != 0) {
                rlc_panicf(status, "Unable to release lock");
        }
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

#endif /* RLC_COMMON_H__ */
