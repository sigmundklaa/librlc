
#ifndef RLC_TIMEOUT_H__
#define RLC_TIMEOUT_H__

#include <stdbool.h>

#include <gabs/timer.h>

#include <rlc/utils.h>
#include <rlc/list.h>
#include <rlc/errno.h>

RLC_BEGIN_DECL

struct rlc_context;
struct rlc_timer;

typedef void (*rlc_timer_cb)(struct rlc_timer *, struct rlc_context *);

struct rlc_timer {
        gabs_timer gtimer;

        rlc_timer_cb cb;
        struct rlc_context *ctx;
};

rlc_errno rlc_timer_install(struct rlc_timer *timer, rlc_timer_cb cb,
                            struct rlc_context *ctx);

static inline bool rlc_timer_okay(struct rlc_timer *timer)
{
        return gabs_timer_okay(timer->gtimer);
}

static inline rlc_errno rlc_timer_uninstall(struct rlc_timer *timer)
{
        return gabs_timer_uninstall(timer->gtimer);
}

static inline rlc_errno rlc_timer_start(struct rlc_timer *timer,
                                        uint32_t delay_us)
{
        return gabs_timer_start(timer->gtimer, delay_us);
}

static inline rlc_errno rlc_timer_restart(struct rlc_timer *timer,
                                          uint32_t delay_us)
{
        return gabs_timer_restart(timer->gtimer, delay_us);
}

static inline rlc_errno rlc_timer_stop(struct rlc_timer *timer)
{
        return gabs_timer_stop(timer->gtimer);
}

static inline bool rlc_timer_active(struct rlc_timer *timer)
{
        return gabs_timer_active(timer->gtimer);
}

RLC_END_DECL

#endif /* RLC_TIMEOUT_H__ */
