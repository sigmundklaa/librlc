
#ifndef RLC_PLAT_ZEPHYR_H__
#define RLC_PLAT_ZEPHYR_H__

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <rlc/utils.h>

RLC_BEGIN_DECL

typedef struct k_mutex rlc_lock;
typedef void *rlc_timer;
typedef struct net_buf rlc_buf;

RLC_END_DECL

#endif /* RLC_PLAT_ZEPHYR_H__ */
