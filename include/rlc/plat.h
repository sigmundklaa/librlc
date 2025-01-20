
#ifndef RLC_PLAT_H__
#define RLC_PLAT_H__

#include <pthread.h>

typedef pthread_mutex_t rlc_lock;

void rlc_plat_lock_init(rlc_lock *lock);
void rlc_plat_lock_deinit(rlc_lock *lock);
void rlc_plat_lock_acquire(rlc_lock *);
void rlc_plat_lock_release(rlc_lock *);

#endif /* RLC_PLAT_H__ */
