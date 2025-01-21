
#ifndef RLC_LOCK_H__
#define RLC_LOCK_H__

#include <rlc/utils.h>
#include <rlc/plat.h>

RLC_BEGIN_DECL

static inline void rlc_lock_init(rlc_lock *lock)
{
        rlc_plat_lock_init(lock);
}

static inline void rlc_lock_deinit(rlc_lock *lock)
{
        rlc_plat_lock_deinit(lock);
}

static inline void rlc_lock_acquire(rlc_lock *lock)
{
        rlc_plat_lock_acquire(lock);
}

static inline void rlc_lock_release(rlc_lock *lock)
{
        rlc_plat_lock_release(lock);
}

RLC_END_DECL

#endif /* RLC_LOCK_H__ */
