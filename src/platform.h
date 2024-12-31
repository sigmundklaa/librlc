
#ifndef RLC_PLATFORM_H__
#define RLC_PLATFORM_H__

#include <rlc/rlc.h>

RLC_BEGIN_DECL

typedef void rlc_plat_mx;

uint64_t rlc_plat_time_ms(void);

void rlc_plat_mx_lock(rlc_plat_mx *mx);
void rlc_plat_mx_unlock(rlc_plat_mx *mx);

#define rlc_plat_mx_locked(mx_)                                                \
        int i = (rlc_plat_mx_lock(mx_), 0);                                    \
        i < 1;                                                                 \
        i++, rlc_plat_mx_unlock(mx_)

RLC_END_DECL

#endif /* RLC_PLATFORM_H__ */
