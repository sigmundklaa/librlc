
#ifndef RLC_PLAT_H__
#define RLC_PLAT_H__

#include <rlc/utils.h>

/* This header should be defined by the platform target */
#include <rlc_plat.h>

RLC_BEGIN_DECL

struct rlc_context;

void rlc_plat_init(void);

void rlc_plat_lock_init(rlc_lock *lock);
void rlc_plat_lock_deinit(rlc_lock *lock);
void rlc_plat_lock_acquire(rlc_lock *);
void rlc_plat_lock_release(rlc_lock *);

bool rlc_plat_timer_okay(rlc_timer timer);
rlc_timer rlc_plat_timer_install(void (*cb)(rlc_timer, struct rlc_context *),
                                 struct rlc_context *ctx);

rlc_errno rlc_plat_timer_uninstall(rlc_timer timer);

rlc_errno rlc_plat_timer_start(rlc_timer timer, uint32_t delay_us);
rlc_errno rlc_plat_timer_restart(rlc_timer timer, uint32_t delay_us);
rlc_errno rlc_plat_timer_stop(rlc_timer timer);

bool rlc_plat_timer_active(rlc_timer timer);

RLC_END_DECL

#endif /* RLC_PLAT_H__ */
