
#ifndef RLC_PLAT_LINUX_H__
#define RLC_PLAT_LINUX_H__

#define RLC_PLAT_LINUX

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdbool.h>

#include <rlc/decl.h>

#include "timer.h"

RLC_BEGIN_DECL

struct rlc_linux_timer_manager;

typedef struct rlc_linux_timer_info *rlc_timer;

typedef struct rlc_platform_linux {
        struct rlc_linux_timer_manager timer_man;
} rlc_platform;

RLC_END_DECL

#endif /* RLC_PLAT_LINUX_H__ */
