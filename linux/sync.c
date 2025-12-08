
#include <errno.h>

#include <rlc/rlc.h>
#include <rlc/plat.h>

#include "utils.h"

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

void rlc_plat_sem_init(rlc_sem *sem, unsigned int initval)
{
        int status;

        status = sem_init(sem, 0, initval);
        if (status < 0) {
                rlc_panicf(errno, "Sem init failure");
        }
}

void rlc_plat_sem_deinit(rlc_sem *sem)
{
        int status;

        status = sem_destroy(sem);
        if (status < 0) {
                rlc_panicf(errno, "Sem deinit failure");
        }
}

void rlc_plat_sem_up(rlc_sem *sem)
{
        int status;

        status = sem_post(sem);
        if (status < 0) {
                rlc_panicf(errno, "Sem up");
        }
}

rlc_errno rlc_plat_sem_down(rlc_sem *sem, int64_t timeout_us)
{
        int status;
        struct timespec timeout_abs;

        if (timeout_us == 0) {
                status = sem_trywait(sem);
        } else if (timeout_us == -1) {
                status = sem_wait(sem);
        } else {
                status = clock_gettime(CLOCK_REALTIME, &timeout_abs);
                if (status < 0) {
                        return -errno;
                }

                timeout_abs.tv_sec += ((uint64_t)timeout_us / (uint64_t)1e6);
                timeout_abs.tv_nsec +=
                        ((uint64_t)timeout_us * (uint64_t)1e3) % (uint64_t)1e9;

                if (timeout_abs.tv_nsec >= 1e9) {
                        timeout_abs.tv_sec++;
                        timeout_abs.tv_nsec -= 1e9;
                }

                status = sem_timedwait(sem, &timeout_abs);
                if (status < 0 && errno == ETIMEDOUT) {
                        return -EAGAIN;
                }
        }

        if (status < 0) {
                return -errno;
        }

        return 0;
}
