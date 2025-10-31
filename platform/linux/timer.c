
#include <pthread.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <poll.h>

#include <rlc/timer.h>
#include <rlc/rlc.h>
#include <rlc/sync.h>

#include "utils.h"
#include "log.h"

enum timer_state {
        TIMER_STALE,
        TIMER_ACTIVE,
        TIMER_FIRING,
};

struct timer_info {
        rlc_timer_cb cb;
        struct rlc_context *ctx;

        int fd;
        rlc_timer id;
        enum timer_state state;
        enum timer_state next_state;
        unsigned int flags;

        struct timer_info *next;
};

struct dyn_array {
        void *mem;
        size_t size;
        size_t cap;
        size_t elem_size;
};

static rlc_lock lock_;
static pthread_t thread_h_;
static struct timer_info *timer_list_;
static rlc_timer next_id_;
static int event_fd_; /* Used to wake up the worker thread to readjust the
                         fd_set */

static void timer_del_(rlc_timer id);

static void *dyn_array_push(struct dyn_array *arr)
{
        size_t new_cap;

        if (arr->mem == NULL || arr->size >= arr->cap) {
                new_cap = rlc_max(1, arr->cap * 2);

                arr->mem = realloc(arr->mem, arr->elem_size * new_cap);
                if (arr->mem == NULL) {
                        return NULL;
                }

                arr->cap = new_cap;
        }

        return (uint8_t *)arr->mem + (arr->elem_size * arr->size++);
}

static void pfd_push(struct dyn_array *arr, int fd)
{
        struct pollfd *pfd;

        pfd = dyn_array_push(arr);
        if (pfd == NULL) {
                rlc_panicf(ENOMEM, "Unable to poll timer");
                return;
        }

        pfd->fd = fd;
        pfd->events = POLLIN;
        pfd->revents = 0;
}

static struct timer_info *timer_from_fd_(int fd)
{
        struct timer_info *t;

        for (rlc_each_node(timer_list_, t, next)) {
                if (t->fd == fd) {
                        return t;
                }
        }

        return NULL;
}

static void timer_alarm_(struct timer_info *t)
{
        if (t->cb == NULL) {
                return;
        }

        rlc_timer_alarm(t->id, t->ctx, t->cb);
}

static void *worker_(void *arg)
{
        int num_ready;
        size_t i;
        uint64_t dummy;
        struct timer_info *cur;
        struct pollfd *pfd;
        struct dyn_array pfds;

        (void)arg;

        pfds.mem = NULL;
        pfds.cap = 0;
        pfds.elem_size = sizeof(*pfd);

        for (;;) {
                pfds.size = 0;

                pfd_push(&pfds, event_fd_);

                rlc_lock_acquire(&lock_);

                for (rlc_each_node(timer_list_, cur, next)) {
                        cur->state = cur->next_state;

                        if (cur->state != TIMER_ACTIVE) {
                                continue;
                        }

                        pfd_push(&pfds, cur->fd);
                }

                rlc_lock_release(&lock_);

                num_ready = poll(pfds.mem, pfds.size, -1);
                if (num_ready < 0) {
                        rlc_errf("Polling timers returned %" RLC_PRI_ERRNO,
                                 errno);
                        continue;
                }

                rlc_lock_acquire(&lock_);

                pfd = &((struct pollfd *)pfds.mem)[0];
                if (pfd->revents & POLLIN) {
                        /* Reset set of fds being watched */
                        rlc_dbgf("Timer thread reloaded");
                        (void)read(pfd->fd, &dummy, sizeof(dummy));

                        rlc_lock_release(&lock_);
                        continue;
                }

                for (i = 1; i < pfds.size; i++) {
                        pfd = &((struct pollfd *)pfds.mem)[i];

                        if (pfd->revents & POLLIN) {
                                rlc_dbgf("Timer alarm");
                                (void)read(pfd->fd, &dummy, sizeof(dummy));

                                cur = timer_from_fd_(pfd->fd);
                                if (cur == NULL) {
                                        rlc_errf("Unrecognized timer fd: %i",
                                                 pfd->fd);
                                        continue;
                                }

                                cur->state = TIMER_FIRING;
                                cur->next_state = TIMER_FIRING;

                                rlc_lock_release(&lock_);

                                timer_alarm_(cur);
                                rlc_lock_acquire(&lock_);

                                if (cur->flags & RLC_TIMER_SINGLE) {
                                        timer_del_(cur->id);
                                } else if (cur->next_state == TIMER_FIRING) {
                                        /* If state has not been changed by
                                         * a call within the callback, then make
                                         * the timer go stale. */
                                        cur->next_state = TIMER_STALE;
                                }
                        }
                }

                rlc_lock_release(&lock_);
        }

        return NULL;
}

static struct timer_info *timer_add_(rlc_timer_cb cb, struct rlc_context *ctx,
                                     unsigned int flags)
{
        struct timer_info *t;
        struct timer_info *cur;
        struct timer_info **lastp;

        t = malloc(sizeof(*t));
        if (t == NULL) {
                return NULL;
        }

        t->next = NULL;
        t->ctx = ctx;
        t->cb = cb;
        t->id = next_id_++;
        t->next_state = TIMER_STALE;
        t->state = TIMER_STALE;
        t->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        t->flags = flags;

        if (t->fd < 0) {
                free(t);
                return NULL;
        }

        lastp = &timer_list_;

        for (rlc_each_node(timer_list_, cur, next)) {
                lastp = &cur->next;
        }

        *lastp = t;
        return t;
}

static struct timer_info *timer_get_(rlc_timer id)
{
        struct timer_info *cur;

        for (rlc_each_node(timer_list_, cur, next)) {
                if (cur->id == id) {
                        return cur;
                }
        }

        return NULL;
}

static void timer_del_(rlc_timer id)
{
        struct timer_info *cur;
        struct timer_info **lastp;

        lastp = &timer_list_;

        for (rlc_each_node(timer_list_, cur, next)) {
                if (cur->id == id) {
                        *lastp = cur->next;
                        close(cur->fd);
                        free(cur);
                        return;
                }

                lastp = &cur->next;
        }
}

static void trigger_reset_(void)
{
        uint64_t count;
        int size;

        count = 1;

        size = write(event_fd_, &count, sizeof(count));
        rlc_assert(size == sizeof(count));

        rlc_dbgf("Triggering reset");
}

void rlc_linux_timer_api_init(void)
{
        int status;

        rlc_lock_init(&lock_);

        event_fd_ = eventfd(0, EFD_SEMAPHORE);
        if (event_fd_ < 0) {
                rlc_panicf(errno, "Unable to create eventfd");
                return;
        }

        status = pthread_create(&thread_h_, NULL, worker_, NULL);
        if (status != 0) {
                rlc_panicf(status, "Unable to create thread");
                return;
        }
}

bool rlc_plat_timer_okay(rlc_timer timer)
{
        return timer != -1;
}

rlc_timer rlc_plat_timer_install(rlc_timer_cb cb, struct rlc_context *ctx,
                                 unsigned int flags)
{
        struct timer_info *t;
        rlc_timer id;

        rlc_lock_acquire(&lock_);
        t = timer_add_(cb, ctx, flags);

        if (t == NULL) {
                id = -1;
        } else {
                id = t->id;
        }

        rlc_lock_release(&lock_);
        return id;
}

rlc_errno rlc_plat_timer_uninstall(rlc_timer timer)
{
        rlc_lock_acquire(&lock_);
        trigger_reset_();
        timer_del_(timer);
        rlc_lock_release(&lock_);

        return 0;
}

static void to_itimerspec_(struct itimerspec *spec, uint32_t time_us)
{
        spec->it_interval.tv_sec = 0;
        spec->it_interval.tv_nsec = 0;

        spec->it_value.tv_sec = time_us / (uint32_t)1e6;
        spec->it_value.tv_nsec = (time_us % (uint32_t)1e6) * 1000;

        if (spec->it_value.tv_sec == 0 && spec->it_value.tv_nsec == 0) {
                spec->it_value.tv_nsec = 1;
        }
}

static rlc_errno timer_restart_(struct timer_info *t, uint32_t delay_us)
{
        rlc_errno status;
        struct itimerspec spec;
        uint64_t dummy;
        size_t drain_size;

        t->next_state = TIMER_ACTIVE;

        to_itimerspec_(&spec, delay_us);

        status = timerfd_settime(t->fd, 0, &spec, NULL);
        trigger_reset_();

        if (status < 0) {
                return -errno;
        }

        return status;
}

rlc_errno rlc_plat_timer_start(rlc_timer timer, uint32_t delay_us)
{
        rlc_errno status;
        struct timer_info *t;

        rlc_lock_acquire(&lock_);

        t = timer_get_(timer);
        if (t == NULL) {
                rlc_lock_release(&lock_);
                return -EINVAL;
        }

        if (t->state != TIMER_STALE || t->next_state != TIMER_STALE) {
                rlc_lock_release(&lock_);
                return -EBUSY;
        }

        status = timer_restart_(t, delay_us);
        rlc_lock_release(&lock_);

        return status;
}

rlc_errno rlc_plat_timer_restart(rlc_timer timer, uint32_t delay_us)
{
        rlc_errno status;
        struct timer_info *t;

        rlc_lock_acquire(&lock_);

        t = timer_get_(timer);
        if (t == NULL) {
                rlc_lock_release(&lock_);
                return -EINVAL;
        }

        status = timer_restart_(t, delay_us);
        rlc_lock_release(&lock_);

        return status;
}

rlc_errno rlc_plat_timer_stop(rlc_timer timer)
{
        rlc_errno status;
        struct timer_info *t;
        struct itimerspec spec;

        rlc_lock_acquire(&lock_);

        trigger_reset_();

        t = timer_get_(timer);
        if (t == NULL) {
                rlc_lock_release(&lock_);
                return -EINVAL;
        }

        spec = (struct itimerspec){0};
        status = timerfd_settime(t->fd, 0, &spec, NULL);
        if (status < 0) {
                status = -errno;
        }

        t->next_state = TIMER_STALE;

        rlc_lock_release(&lock_);

        return status;
}

bool rlc_plat_timer_active(rlc_timer timer)
{
        struct timer_info *t;
        bool ret;

        rlc_lock_acquire(&lock_);
        t = timer_get_(timer);
        /* Use a counter/id to track changes */
        ret = t != NULL && t->state == t->next_state && t->state != TIMER_STALE;
        rlc_lock_release(&lock_);

        return ret;
}

unsigned int rlc_plat_timer_flags(rlc_timer timer)
{
        struct timer_info *t;
        unsigned int flags;

        rlc_lock_acquire(&lock_);

        t = timer_get_(timer);
        rlc_assert(t != NULL);

        flags = t->flags;

        rlc_lock_release(&lock_);

        return flags;
}
