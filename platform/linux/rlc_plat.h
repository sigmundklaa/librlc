
#ifndef RLC_PLAT_LINUX_H__
#define RLC_PLAT_LINUX_H__

#include <rlc/utils.h>
#include <pthread.h>

RLC_BEGIN_DECL

typedef pthread_mutex_t rlc_lock;
typedef int rlc_timer;

typedef struct rlc_buf {
        uint8_t *data;
        size_t size;
        size_t cap;

        bool readonly;
        unsigned int refcnt;

        struct rlc_buf *next;
} rlc_buf;

RLC_END_DECL

#endif /* RLC_PLAT_LINUX_H__ */
