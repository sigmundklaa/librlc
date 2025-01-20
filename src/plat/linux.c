
#include <rlc/rlc.h>
#include <rlc/plat.h>

#include "../utils.h"

void rlc_plat_lock_init(rlc_lock *lock)
{
        int status;
        pthread_mutexattr_t attr;

        (void)pthread_mutexattr_init(&attr);
        (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

        status = pthread_mutex_init(lock, &attr);
        if (status != 0) {
                rlc_assert(0);
        }
}

void rlc_plat_lock_deinit(rlc_lock *lock)
{
        int status;

        status = pthread_mutex_destroy(lock);
        if (status != 0) {
                rlc_assert(0);
        }
}

void rlc_plat_lock_acquire(rlc_lock *lock)
{
        int status;

        status = pthread_mutex_lock(lock);
        if (status != 0) {
                rlc_assert(0);
        }
}

void rlc_plat_lock_release(rlc_lock *lock)
{
        int status;

        status = pthread_mutex_unlock(lock);
        if (status != 0) {
                rlc_assert(0);
        }
}
