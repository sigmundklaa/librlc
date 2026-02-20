
#ifndef RLC_UTILS_H__
#define RLC_UTILS_H__

#include <assert.h>

#ifdef __cplusplus
#define RLC_BEGIN_DECL extern "C" {
#define RLC_END_DECL   }
#else
#define RLC_BEGIN_DECL
#define RLC_END_DECL
#endif


RLC_BEGIN_DECL

#define rlc_max(a, b)   (((a) > (b)) ? (a) : (b))
#define rlc_min(a, b)   (((a) < (b)) ? (a) : (b))
#define rlc_assert(...) assert(__VA_ARGS__)

#define rlc_mod(x) x

#define rlc_panicf(status_, fmt_, ...)                                         \
        do {                                                                   \
                rlc_assert(0);                                                 \
        } while (0)

#define rlc_array_size(x) (sizeof(x) / sizeof((x)[0]))

RLC_END_DECL

#endif /* RLC_UTILS_H__ */
