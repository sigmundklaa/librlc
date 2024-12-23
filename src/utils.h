
#ifndef RLC_UTILS_H__
#define RLC_UTILS_H__

#include <assert.h>

#include <rlc/rlc.h>

#include "log.h"

RLC_BEGIN_DECL

#define rlc_max(a, b)   (((a) > (b)) ? (a) : (b))
#define rlc_min(a, b)   (((a) < (b)) ? (a) : (b))
#define rlc_assert(...) assert(__VA_ARGS__)

#define rlc_mod(x) x

#define rlc_panicf(status_, fmt_, ...)                                         \
        do {                                                                   \
                rlc_errf(fmt_ " (paniced with status %" RLC_PRI_ERRNO ")",     \
                         ##__VA_ARGS__, status_);                              \
                rlc_assert(0);                                                 \
        } while (0)

RLC_END_DECL

#endif /* RLC_UTILS_H__ */
