
#ifndef RLC_PLAT_ZEPHYR_LOG_H__
#define RLC_PLAT_ZEPHYR_LOG_H__

#include <rlc/decl.h>
#include <zephyr/logging/log.h>

RLC_BEGIN_DECL

LOG_MODULE_DECLARE(rlc, CONFIG_RLC_LOG_LEVEL);

#define rlc_plat_dbgf(fmt_, ...) LOG_DBG(fmt_, ##__VA_ARGS__)
#define rlc_plat_inff(fmt_, ...) LOG_INF(fmt_, ##__VA_ARGS__)
#define rlc_plat_wrnf(fmt_, ...) LOG_WRN(fmt_, ##__VA_ARGS__)
#define rlc_plat_errf(fmt_, ...) LOG_ERR(fmt_, ##__VA_ARGS__)

RLC_END_DECL

#endif /* RLC_PLAT_ZEPHYR_LOG_H__ */
