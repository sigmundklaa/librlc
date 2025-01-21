
#ifndef RLC_UTILS_H__
#define RLC_UTILS_H__

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>

#ifdef __cplusplus
#define RLC_BEGIN_DECL extern "C" {
#define RLC_END_DECL   }
#else
#define RLC_BEGIN_DECL
#define RLC_END_DECL
#endif

RLC_BEGIN_DECL

typedef int32_t rlc_errno;
#define RLC_PRI_ERRNO PRIi32

#define rlc_max(a, b)   (((a) > (b)) ? (a) : (b))
#define rlc_min(a, b)   (((a) < (b)) ? (a) : (b))
#define rlc_assert(...) assert(__VA_ARGS__)

#define rlc_mod(x) x

#define rlc_ascii_cmd__(c_)  "\e[" c_ "m"
#define rlc_color_bold__(c_) rlc_ascii_cmd__("1;" c_)
#define rlc_color__(c_)      rlc_ascii_cmd__("0;" c_)
#define rlc_color_red__      rlc_color_bold__("31")
#define rlc_color_green__    rlc_color_bold__("32")
#define rlc_color_yellow__   rlc_color_bold__("33")
#define rlc_color_cyan__     rlc_color_bold__("36")
#define rlc_ascii_reset__    rlc_ascii_cmd__("0")

#define rlc_logf__(lvl_, color_, fmt_, ...)                                    \
        (void)printf("[" color_ lvl_ rlc_ascii_reset__ "]: " fmt_ "\n",        \
                     ##__VA_ARGS__)

#define rlc_dbgf(fmt_, ...)                                                    \
        rlc_logf__("DBG", rlc_color_cyan__, fmt_, ##__VA_ARGS__)
#define rlc_inff(fmt_, ...)                                                    \
        rlc_logf__("INF", rlc_color_green__, fmt_, ##__VA_ARGS__)
#define rlc_wrnf(fmt_, ...)                                                    \
        rlc_logf__("WRN", rlc_color_yellow__, fmt_, ##__VA_ARGS__)
#define rlc_errf(fmt_, ...)                                                    \
        rlc_logf__("ERR", rlc_color_red__, fmt_, ##__VA_ARGS__)

#define rlc_panicf(status_, fmt_, ...)                                         \
        do {                                                                   \
                rlc_errf(fmt_ " (paniced with status %" RLC_PRI_ERRNO ")",     \
                         ##__VA_ARGS__, status_);                              \
                exit(status_);                                                 \
        } while (0)

RLC_END_DECL

#endif /* RLC_UTILS_H__ */
