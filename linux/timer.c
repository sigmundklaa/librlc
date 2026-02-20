
#include <pthread.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <stdatomic.h>

#include <rlc/timer.h>
#include <rlc/rlc.h>

#include "timer.h"
#include "log.h"
#include "atomic_mask.h"

#define BIT_USED   (1 << 0) /* Set by client, unset by worker */
#define BIT_ZOMBIE (1 << 1) /* Set by client, unset by worker */
#define BIT_ACTIVE (1 << 2) /* Set/unset by worker, unset by client */
#define BIT_SCHED  (1 << 3) /* Set by client, unset by worker */

enum {
        WORK_STATE_NORMAL,
        WORK_STATE_EXIT,
        WORK_STATE_RESET,
};

static struct rlc_linux_timer_manager *get_man(struct rlc_context *ctx)
{
        return &ctx->platform.timer_man;
}

static void trigger_reset(struct rlc_linux_timer_manager *man)
{
        uint64_t count;
        int size;

        count = 1;

        size = write(man->event_fd, &count, sizeof(count));
        rlc_assert(size == sizeof(count));

        gabs_log_dbgf(man->logger, "Triggering reset");
}

static void to_itimerspec(struct itimerspec *spec, uint32_t time_us)
{
        spec->it_interval.tv_sec = 0;
        spec->it_interval.tv_nsec = 0;

        spec->it_value.tv_sec = time_us / (uint32_t)1e6;
        spec->it_value.tv_nsec = (time_us % (uint32_t)1e6) * 1000;

        if (spec->it_value.tv_sec == 0 && spec->it_value.tv_nsec == 0) {
                spec->it_value.tv_nsec = 1;
        }
}

static rlc_errno timer_restart(struct rlc_linux_timer_info *t,
                               uint32_t delay_us)
{
        rlc_errno status;
        struct itimerspec spec;

        /* Make sure timer does not fire and/or start before we schedule
         * it again. */
        mask_clear(&t->state, BIT_ACTIVE | BIT_SCHED);

        to_itimerspec(&spec, delay_us);

        status = timerfd_settime(t->fd, 0, &spec, NULL);

        mask_set(&t->state, BIT_SCHED);
        trigger_reset(get_man(t->ctx));

        if (status < 0) {
                return -errno;
        }

        return status;
}

static bool timer_valid(struct rlc_linux_timer_info *t)
{
        return mask_get(&t->state, BIT_USED | BIT_ZOMBIE) == BIT_USED;
}

static void timer_cleanup(struct rlc_linux_timer_info *t)
{
        close(t->fd);

        /* Reset state to default */
        atomic_store(&t->state, 0);
}

static struct rlc_linux_timer_info *
timer_from_fd(struct rlc_linux_timer_manager *man, int fd)
{
        struct rlc_linux_timer_info *t;
        size_t i;

        for (i = 0; i < rlc_array_size(man->timers); i++) {
                t = &man->timers[i];

                if (!timer_valid(t)) {
                        continue;
                }

                if (t->fd == fd) {
                        return t;
                }
        }

        return NULL;
}

static struct rlc_linux_timer_info *
timer_alloc(struct rlc_linux_timer_manager *man)
{
        struct rlc_linux_timer_info *cur;
        size_t i;

        for (i = 0; i < rlc_array_size(man->timers); i++) {
                cur = &man->timers[i];

                if (mask_set_strict(&cur->state, BIT_USED)) {
                        return cur;
                }
        }

        return NULL;
}

static struct rlc_linux_timer_info *
timer_add(rlc_timer_cb cb, struct rlc_context *ctx, unsigned int flags)
{
        struct rlc_linux_timer_info *t;
        struct rlc_linux_timer_info *cur;

        t = timer_alloc(get_man(ctx));
        if (t == NULL) {
                return NULL;
        }

        t->ctx = ctx;
        t->cb = cb;
        t->state = 0;
        t->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        t->flags = flags;

        if (t->fd < 0) {
                return NULL;
        }

        mask_set(&t->state, BIT_USED);

        return t;
}

static void timer_alarm(struct rlc_linux_timer_info *t)
{
        if (t->cb == NULL) {
                return;
        }

        rlc_timer_alarm(t, t->ctx, t->cb);
}

static void pfd_push(struct pollfd **arrptr, int fd)
{
        struct pollfd *pfd;

        pfd = *arrptr;
        pfd->fd = fd;
        pfd->events = POLLIN;
        pfd->revents = 0;

        *arrptr += 1;
}

static void reset_timers(struct rlc_linux_timer_manager *man)
{
        struct rlc_linux_timer_info *cur;
        size_t i;

        for (i = 0; i < rlc_array_size(man->timers); i++) {
                cur = &man->timers[i];

                if (!mask_test(&cur->state, BIT_USED)) {
                        continue;
                }

                if (cur->flags & RLC_TIMER_SINGLE) {
                        timer_cleanup(cur);
                } else {
                        mask_clear(&cur->state, BIT_ACTIVE | BIT_SCHED);
                }
        }
}

static void *worker_(void *man_arg)
{
        struct rlc_linux_timer_manager *man;
        int num_ready;
        size_t count;
        uint64_t dummy;
        unsigned int state;
        size_t i;
        struct rlc_linux_timer_info *cur;
        struct pollfd *pfd;
        struct pollfd pfds[RLC_LINUX_TIMER_COUNT_MAX];

        man = man_arg;

        for (;;) {
                pfd = pfds;
                pfd_push(&pfd, man->event_fd);

                state = atomic_load(&man->work_state);

                if (state == WORK_STATE_EXIT) {
                        gabs_log_inff(man->logger,
                                      "Exiting timer worker thread");
                        break;
                } else if (state == WORK_STATE_RESET) {
                        reset_timers(man);

                        atomic_store(&man->work_state, WORK_STATE_NORMAL);
                        continue;
                } else {
                        for (i = 0; i < rlc_array_size(man->timers); i++) {
                                cur = &man->timers[i];
                                if (!mask_test(&cur->state, BIT_USED)) {
                                        continue;
                                }

                                if (mask_test(&cur->state, BIT_ZOMBIE)) {
                                        timer_cleanup(cur);
                                        continue;
                                }

                                /* Set active if scheduled bit is set */
                                if (mask_test(&cur->state, BIT_ACTIVE) ||
                                    mask_cas(&cur->state, BIT_SCHED,
                                             BIT_ACTIVE)) {
                                        pfd_push(&pfd, cur->fd);
                                        mask_clear(&cur->state, BIT_SCHED);
                                }
                        }
                }

                count = pfd - pfds;

                num_ready = poll(pfds, count, -1);
                if (num_ready < 0) {
                        gabs_log_errf(man->logger,
                                      "Polling timers returned %" RLC_PRI_ERRNO,
                                      errno);
                        continue;
                }

                pfd = &pfds[0];
                if (pfd->revents & POLLIN) {
                        /* Reset set of fds being watched */
                        gabs_log_dbgf(man->logger, "Timer thread reloaded");
                        (void)read(pfd->fd, &dummy, sizeof(dummy));
                        continue;
                }

                for (; pfd < &pfds[count]; pfd++) {
                        if (pfd->revents & POLLIN) {
                                gabs_log_dbgf(man->logger, "Timer alarm");
                                (void)read(pfd->fd, &dummy, sizeof(dummy));

                                cur = timer_from_fd(man, pfd->fd);
                                if (cur == NULL) {
                                        gabs_log_errf(
                                                man->logger,
                                                "Unrecognized timer fd: %i",
                                                pfd->fd);
                                        continue;
                                }

                                /* Reset by client */
                                if (!mask_test(&cur->state, BIT_ACTIVE)) {
                                        continue;
                                }

                                timer_alarm(cur);

                                if (cur->flags & RLC_TIMER_SINGLE) {
                                        mask_set(&cur->state, BIT_ZOMBIE);
                                } else {
                                        mask_clear(&cur->state, BIT_ACTIVE);
                                }
                        }
                }
        }

        return NULL;
}

int rlc_linux_timer_manager_init(struct rlc_linux_timer_manager *man,
                                 struct rlc_context *ctx)
{
        int status;

        man->logger = ctx->logger;

        man->event_fd = eventfd(0, EFD_SEMAPHORE);
        if (man->event_fd < 0) {
                rlc_panicf(errno, "Unable to create eventfd");
                return -errno;
        }

        status = pthread_create(&man->thread_h, NULL, worker_, man);
        if (status != 0) {
                rlc_panicf(status, "Unable to create thread");
                return -errno;
        }

        return 0;
}

int rlc_linux_timer_manager_reset(struct rlc_linux_timer_manager *man)
{
        atomic_store(&man->work_state, WORK_STATE_RESET);
        trigger_reset(man);

        /* Wait for worker thread to go out of reset */
        while (atomic_load(&man->work_state) == WORK_STATE_RESET) {
        }

        return 0;
}

int rlc_linux_timer_manager_deinit(struct rlc_linux_timer_manager *man)
{
        int status;

        atomic_store(&man->work_state, WORK_STATE_EXIT);
        trigger_reset(man);

        status = pthread_join(man->thread_h, NULL);
        if (status != 0) {
                rlc_panicf(status, "Failed to join thread");
        }

        (void)close(man->event_fd);

        return 0;
}

bool rlc_plat_timer_okay(struct rlc_linux_timer_info *t)
{
        return t != NULL;
}

struct rlc_linux_timer_info *rlc_plat_timer_install(rlc_timer_cb cb,
                                                    struct rlc_context *ctx,
                                                    unsigned int flags)
{
        struct rlc_linux_timer_info *t;
        rlc_timer id;

        return timer_add(cb, ctx, flags);
}

rlc_errno rlc_plat_timer_uninstall(struct rlc_linux_timer_info *t)
{
        (void)rlc_plat_timer_stop(t);

        /* Perform cleanup when worker is done */
        mask_set(&t->state, BIT_ZOMBIE);

        return 0;
}

rlc_errno rlc_plat_timer_start(struct rlc_linux_timer_info *t,
                               uint32_t delay_us)
{
        rlc_errno status;

        /* Already scheduled or running */
        if (mask_get(&t->state, BIT_ACTIVE | BIT_SCHED)) {
                return -EBUSY;
        }

        status = timer_restart(t, delay_us);

        return status;
}

rlc_errno rlc_plat_timer_restart(struct rlc_linux_timer_info *t,
                                 uint32_t delay_us)
{
        return timer_restart(t, delay_us);
}

rlc_errno rlc_plat_timer_stop(struct rlc_linux_timer_info *t)
{
        rlc_errno status;
        struct itimerspec spec;

        mask_clear(&t->state, BIT_ACTIVE | BIT_SCHED);

        /* Reset timers so that we don't get a spurious event when the previous
         * time elapses. */
        spec = (struct itimerspec){0};
        status = timerfd_settime(t->fd, 0, &spec, NULL);
        if (status < 0) {
                status = -errno;
        }

        trigger_reset(get_man(t->ctx));

        return status;
}

bool rlc_plat_timer_active(struct rlc_linux_timer_info *t)
{
        return mask_test(&t->state, BIT_ACTIVE);
}

unsigned int rlc_plat_timer_flags(struct rlc_linux_timer_info *t)
{
        return t->flags;
}
