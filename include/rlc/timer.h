
#ifndef RLC_TIMEOUT_H__
#define RLC_TIMEOUT_H__

#include <rlc/utils.h>
#include <rlc/plat.h>

RLC_BEGIN_DECL

struct rlc_context;
typedef void (*rlc_timer_cb)(rlc_timer, struct rlc_context *);

/**
 * @brief Invoke timer alarm callback
 *
 * This is called by platform code
 */
void rlc_timer_alarm(rlc_timer timer, struct rlc_context *ctx, rlc_timer_cb cb);

static inline bool rlc_timer_okay(rlc_timer timer)
{
        return rlc_plat_timer_okay(timer);
}

static inline rlc_timer rlc_timer_install(rlc_timer_cb cb,
                                          struct rlc_context *ctx)
{
        return rlc_plat_timer_install(cb, ctx);
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
