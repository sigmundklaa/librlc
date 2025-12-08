
#include <zephyr/kernel.h>

#include <rlc/plat.h>

void rlc_plat_lock_init(rlc_lock *lock)
{
        k_mutex_init(lock);
}

void rlc_plat_lock_deinit(rlc_lock *lock)
{
        ARG_UNUSED(lock);
}

void rlc_plat_lock_acquire(rlc_lock *lock)
{
        k_mutex_lock(lock, K_FOREVER);
}

void rlc_plat_lock_release(rlc_lock *lock)
{
        k_mutex_unlock(lock);
}

void rlc_plat_sem_init(rlc_sem *sem, unsigned int initval)
{
        int status;

        status = k_sem_init(sem, initval, K_SEM_MAX_LIMIT);
        __ASSERT_NO_MSG(status == 0);
}

void rlc_plat_sem_deinit(rlc_sem *sem)
{
        ARG_UNUSED(sem);
}

void rlc_plat_sem_up(rlc_sem *sem)
{
        k_sem_give(sem);
}

rlc_errno rlc_plat_sem_down(rlc_sem *sem, int64_t timeout_us)
{
        int status;
        k_timeout_t timeout;

        if (timeout_us == 0) {
                timeout = K_NO_WAIT;
        } else if (timeout_us == -1) {
                timeout = K_FOREVER;
        } else {
                timeout = K_USEC(timeout_us);
        }

        status = k_sem_take(sem, timeout);
        if (status == -EBUSY) {
                status = -EAGAIN;
        }

        return status;
}
