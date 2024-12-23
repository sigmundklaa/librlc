
#ifndef RLC_LINUX_LOG_H__
#define RLC_LINUX_LOG_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>

#include <rlc/decl.h>

RLC_BEGIN_DECL

#ifndef RLC_LOG_LEVEL
#define RLC_LOG_LEVEL (RLC_LOG_LEVEL_DBG)
#endif

#define RLC_LOG_LEVEL_ERR (0)
#define RLC_LOG_LEVEL_WRN (1)
#define RLC_LOG_LEVEL_INF (2)
#define RLC_LOG_LEVEL_DBG (3)

#define rlc_ascii_cmd__(c_)  "\e[" c_ "m"
#define rlc_color_bold__(c_) rlc_ascii_cmd__("1;" c_)
#define rlc_color__(c_)      rlc_ascii_cmd__("0;" c_)
#define rlc_color_red__      rlc_color_bold__("31")
#define rlc_color_green__    rlc_color_bold__("32")
#define rlc_color_yellow__   rlc_color_bold__("33")
#define rlc_color_cyan__     rlc_color_bold__("36")
#define rlc_ascii_reset__    rlc_ascii_cmd__("0")

#define rlc_plat_logf__(lvl_, lvl_fmt_, color_, fmt_, ...)                     \
        do {                                                                   \
                int rlc_log_status__;                                          \
                struct timespec rlc_log_spec__;                                \
                if (lvl_ > RLC_LOG_LEVEL) {                                    \
                        break;                                                 \
                }                                                              \
                rlc_log_status__ =                                             \
                        clock_gettime(CLOCK_MONOTONIC, &rlc_log_spec__);       \
                if (rlc_log_status__ < 0) {                                    \
                        rlc_log_spec__.tv_sec = 0;                             \
                        rlc_log_spec__.tv_nsec = 0;                            \
                }                                                              \
                                                                               \
                (void)printf(                                                  \
                        "[" color_ lvl_fmt_ rlc_ascii_reset__ ": %" PRIu32     \
                        ".%" PRIu32 ".%" PRIu32 "] " fmt_ "\n",                \
                        (uint32_t)rlc_log_spec__.tv_sec,                       \
                        (uint32_t)(rlc_log_spec__.tv_nsec / 1e6),              \
                        (uint32_t)((rlc_log_spec__.tv_nsec % (uint64_t)1e6) /  \
                                   1e3),                                       \
                        ##__VA_ARGS__);                                        \
        } while (0)

#define rlc_plat_dbgf(fmt_, ...)                                               \
        rlc_plat_logf__(RLC_LOG_LEVEL_DBG, "DBG", rlc_color_cyan__, fmt_,      \
                        ##__VA_ARGS__)
#define rlc_plat_inff(fmt_, ...)                                               \
        rlc_plat_logf__(RLC_LOG_LEVEL_INF, "INF", rlc_color_green__, fmt_,     \
                        ##__VA_ARGS__)
#define rlc_plat_wrnf(fmt_, ...)                                               \
        rlc_plat_logf__(RLC_LOG_LEVEL_WRN, "WRN", rlc_color_yellow__, fmt_,    \
                        ##__VA_ARGS__)
#define rlc_plat_errf(fmt_, ...)                                               \
        rlc_plat_logf__(RLC_LOG_LEVEL_ERR, "ERR", rlc_color_red__, fmt_,       \
                        ##__VA_ARGS__)

RLC_END_DECL

#endif /* RLC_LINUX_LOG_H__ */
