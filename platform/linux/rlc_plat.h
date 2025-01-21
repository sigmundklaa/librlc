
#ifndef RLC_PLAT_LINUX_H__
#define RLC_PLAT_LINUX_H__

#include <rlc/utils.h>
#include <pthread.h>

RLC_BEGIN_DECL

typedef pthread_mutex_t rlc_lock;
typedef int rlc_timer;

RLC_END_DECL

#endif /* RLC_PLAT_LINUX_H__ */
