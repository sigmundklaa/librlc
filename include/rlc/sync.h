
#ifndef RLC_SYNC_H__
#define RLC_SYNC_H__

#include <rlc/decl.h>
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

static inline void rlc_sem_init(rlc_sem *sem, unsigned int initval)
{
        rlc_plat_sem_init(sem, initval);
}

static inline void rlc_sem_deinit(rlc_sem *sem)
{
        rlc_plat_sem_deinit(sem);
}

static inline void rlc_sem_up(rlc_sem *sem)
{
        rlc_plat_sem_up(sem);
}

static inline rlc_errno rlc_sem_down(rlc_sem *sem, int64_t timeout_us)
{
        return rlc_plat_sem_down(sem, timeout_us);
}

RLC_END_DECL

#endif /* RLC_SYNC_H__ */
