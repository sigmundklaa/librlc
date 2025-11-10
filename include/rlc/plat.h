
#ifndef RLC_PLAT_H__
#define RLC_PLAT_H__

#include <stdbool.h>

#include <rlc/decl.h>
#include <rlc/errno.h>

/* This header should be defined by the platform target */
#include <rlc_plat.h>

RLC_BEGIN_DECL

struct rlc_context;

void rlc_plat_init(void);

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

rlc_buf rlc_plat_buf_alloc(struct rlc_context *ctx, size_t size);
rlc_buf rlc_plat_buf_alloc_ro(struct rlc_context *ctx, uint8_t *data,
                              size_t size);
bool rlc_plat_buf_okay(rlc_buf buf);
void rlc_plat_buf_incref(rlc_buf buf);
void rlc_plat_buf_decref(rlc_buf buf);
rlc_buf rlc_plat_buf_chain_at(rlc_buf buf, rlc_buf next, size_t offset);
rlc_buf rlc_plat_buf_chain_back(rlc_buf buf, rlc_buf back);
rlc_buf rlc_plat_buf_chain_front(rlc_buf buf, rlc_buf front);
rlc_buf rlc_plat_buf_clone(rlc_buf buf, size_t offset, size_t size,
                           struct rlc_context *ctx);
rlc_buf rlc_plat_buf_view(rlc_buf buf, size_t offset, size_t size,
                          struct rlc_context *ctx);
rlc_buf rlc_plat_buf_strip_front(rlc_buf buf, size_t size,
                                 struct rlc_context *ctx);
rlc_buf rlc_plat_buf_strip_back(rlc_buf buf, size_t size,
                                struct rlc_context *ctx);
void rlc_plat_buf_put(rlc_buf buf, const void *mem, size_t size);
size_t rlc_plat_buf_copy(rlc_buf buf, void *mem, size_t offset,
                         size_t max_size);
size_t rlc_plat_buf_cap(rlc_buf buf);
size_t rlc_plat_buf_size(rlc_buf buf);

rlc_buf_ci rlc_plat_buf_ci_init(rlc_buf *buf);
rlc_buf_ci rlc_plat_buf_ci_next(rlc_buf_ci it);
bool rlc_plat_buf_ci_eoi(rlc_buf_ci it);
uint8_t *rlc_plat_buf_ci_data(rlc_buf_ci it);
uint8_t *rlc_plat_buf_ci_reserve_head(rlc_buf_ci it, size_t bytes);
uint8_t *rlc_plat_buf_ci_release_head(rlc_buf_ci it, size_t bytes);
uint8_t *rlc_plat_buf_ci_reserve_tail(rlc_buf_ci it, size_t bytes);
uint8_t *rlc_plat_buf_ci_release_tail(rlc_buf_ci it, size_t bytes);
size_t rlc_plat_buf_ci_headroom(rlc_buf_ci it);
size_t rlc_plat_buf_ci_tailroom(rlc_buf_ci it);
rlc_buf_ci rlc_plat_buf_ci_insert(rlc_buf_ci it, rlc_buf buf);
rlc_buf_ci rlc_plat_buf_ci_remove(rlc_buf_ci it);
void rlc_plat_buf_ci_detach(rlc_buf_ci it);
size_t rlc_plat_buf_ci_cap(rlc_buf_ci it);
size_t rlc_plat_buf_ci_size(rlc_buf_ci it);

RLC_END_DECL

#endif /* RLC_PLAT_H__ */
