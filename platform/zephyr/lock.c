
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
