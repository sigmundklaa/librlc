
#ifndef RLC_LINUX_TIMER_H__
#define RLC_LINUX_TIMER_H__

#include <pthread.h>

#include <rlc/decl.h>
#include <gabs/log.h>

RLC_BEGIN_DECL

#ifdef __cplusplus
#include <atomic>

using atomic_uint = std::atomic<unsigned int>;
#else
#include <stdatomic.h>
#endif

#define RLC_LINUX_TIMER_COUNT_MAX (20)

struct rlc_context;

struct rlc_linux_timer_info {
        void (*cb)(struct rlc_linux_timer_info *, struct rlc_context *);
        struct rlc_context *ctx;

        int fd;
        unsigned int flags;
        atomic_uint state;
};

struct rlc_linux_timer_manager {
        pthread_t thread_h;
        struct rlc_linux_timer_info timers[RLC_LINUX_TIMER_COUNT_MAX];
        int event_fd; /* Used to wake up the worker thread to
                                readjust the fd_set */
        atomic_uint work_state;
        const gabs_logger_h *logger;
};

RLC_END_DECL

#endif /* RLC_LINUX_TIMER_H__ */
