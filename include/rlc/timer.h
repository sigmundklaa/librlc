
#ifndef RLC_TIMEOUT_H__
#define RLC_TIMEOUT_H__

#include <rlc/utils.h>
#include <rlc/plat.h>

RLC_BEGIN_DECL

static inline bool rlc_timer_okay(rlc_timer timer)
{
        return rlc_plat_timer_okay(timer);
}

static inline rlc_timer rlc_timer_install(void (*cb)(rlc_timer, void *),
                                          void *args)
{
        return rlc_plat_timer_install(cb, args);
}

static inline rlc_errno rlc_timer_uninstall(rlc_timer timer)
{
        return rlc_plat_timer_uninstall(timer);
}

static inline rlc_errno rlc_timer_start(rlc_timer timer, uint32_t delay_us)
{
        return rlc_plat_timer_start(timer, delay_us);
}

static inline rlc_errno rlc_timer_restart(rlc_timer timer, uint32_t delay_us)
{
        return rlc_plat_timer_restart(timer, delay_us);
}

static inline rlc_errno rlc_timer_stop(rlc_timer timer)
{
        return rlc_plat_timer_stop(timer);
}

static inline bool rlc_timer_active(rlc_timer timer)
{
        return rlc_plat_timer_active(timer);
}

RLC_END_DECL

#endif /* RLC_TIMEOUT_H__ */
