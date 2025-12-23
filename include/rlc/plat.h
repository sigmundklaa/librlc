
#ifndef RLC_PLAT_H__
#define RLC_PLAT_H__

#include <stdbool.h>

#include <rlc/decl.h>
#include <rlc/errno.h>

/* This header should be defined by the platform target */
#include <rlc_plat.h>

RLC_BEGIN_DECL

struct rlc_context;

rlc_errno rlc_plat_init(rlc_platform *plat);
rlc_errno rlc_plat_reset(rlc_platform *plat);
rlc_errno rlc_plat_deinit(rlc_platform *plat);

void rlc_plat_lock_init(rlc_lock *lock);
void rlc_plat_lock_deinit(rlc_lock *lock);
void rlc_plat_lock_acquire(rlc_lock *);
void rlc_plat_lock_release(rlc_lock *);

void rlc_plat_sem_init(rlc_sem *sem, unsigned int initval);
void rlc_plat_sem_deinit(rlc_sem *sem);
void rlc_plat_sem_up(rlc_sem *sem);
rlc_errno rlc_plat_sem_down(rlc_sem *sem, int64_t timeout_us);

bool rlc_plat_timer_okay(rlc_timer timer);
rlc_timer rlc_plat_timer_install(void (*cb)(rlc_timer, struct rlc_context *),
                                 struct rlc_context *ctx, unsigned int);
rlc_errno rlc_plat_timer_uninstall(rlc_timer timer);
rlc_errno rlc_plat_timer_start(rlc_timer timer, uint32_t delay_us);
rlc_errno rlc_plat_timer_restart(rlc_timer timer, uint32_t delay_us);
rlc_errno rlc_plat_timer_stop(rlc_timer timer);
bool rlc_plat_timer_active(rlc_timer timer);
unsigned int rlc_plat_timer_flags(rlc_timer timer);

RLC_END_DECL

#endif /* RLC_PLAT_H__ */
