
#ifndef RLC_PLAT_ZEPHYR_H__
#define RLC_PLAT_ZEPHYR_H__

#define RLC_PLAT_ZEPHYR

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <rlc/utils.h>

RLC_BEGIN_DECL

typedef void *rlc_timer;

typedef char rlc_platform;

RLC_END_DECL

#endif /* RLC_PLAT_ZEPHYR_H__ */
