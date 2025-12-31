
#ifndef RLC_LINUX_LOG_H__
#define RLC_LINUX_LOG_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>

#include <rlc/decl.h>

RLC_BEGIN_DECL

#define RLC_LINUX_LOG_LEVEL_DBG (0)
#define RLC_LINUX_LOG_LEVEL_INF (1)
#define RLC_LINUX_LOG_LEVEL_WRN (2)
#define RLC_LINUX_LOG_LEVEL_ERR (3)

void rlc_linux_logf(unsigned int level, const char *fmt, ...);

#define rlc_plat_dbgf(fmt_, ...)                                               \
        rlc_linux_logf(RLC_LINUX_LOG_LEVEL_DBG, fmt_, ##__VA_ARGS__)
#define rlc_plat_inff(fmt_, ...)                                               \
        rlc_linux_logf(RLC_LINUX_LOG_LEVEL_INF, fmt_, ##__VA_ARGS__)
#define rlc_plat_wrnf(fmt_, ...)                                               \
        rlc_linux_logf(RLC_LINUX_LOG_LEVEL_WRN, fmt_, ##__VA_ARGS__)
#define rlc_plat_errf(fmt_, ...)                                               \
        rlc_linux_logf(RLC_LINUX_LOG_LEVEL_ERR, fmt_, ##__VA_ARGS__)

RLC_END_DECL

#endif /* RLC_LINUX_LOG_H__ */
