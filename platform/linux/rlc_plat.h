
#ifndef RLC_PLAT_LINUX_H__
#define RLC_PLAT_LINUX_H__

#define RLC_PLAT_LINUX

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdbool.h>

#include <rlc/decl.h>

RLC_BEGIN_DECL

typedef pthread_mutex_t rlc_lock;
typedef int rlc_timer;
typedef sem_t rlc_sem;

struct rlc_buf_frag {
        uint8_t *data;
        size_t size;
        size_t cap;

        bool readonly;
        unsigned int refcnt;

        struct rlc_buf_frag *next;
        struct rlc_context *ctx;
};

typedef struct rlc_buf {
        struct rlc_buf_frag *frags;
} rlc_buf;

typedef struct rlc_buf_ci {
        struct rlc_buf_frag **lastp;
        struct rlc_buf_frag *cur;
} rlc_buf_ci;

RLC_END_DECL

#endif /* RLC_PLAT_LINUX_H__ */
