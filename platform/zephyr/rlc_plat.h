
#ifndef RLC_PLAT_ZEPHYR_H__
#define RLC_PLAT_ZEPHYR_H__

#define RLC_PLAT_ZEPHYR

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <rlc/decl.h>

RLC_BEGIN_DECL

typedef struct k_mutex rlc_lock;
typedef void *rlc_timer;
typedef struct k_sem rlc_sem;

typedef struct rlc_buf {
        struct net_buf *frags;
} rlc_buf;

typedef struct rlc_buf_ci {
        struct net_buf **head;
        struct net_buf *prev;
        struct net_buf *cur;
} rlc_buf_ci;

static inline struct net_buf *rlc_buf_to_zephyr(rlc_buf buf)
{
        return buf.frags;
}

static inline rlc_buf rlc_buf_from_zephyr(struct net_buf *buf)
{
        return (struct rlc_buf){buf};
}

RLC_END_DECL

#endif /* RLC_PLAT_ZEPHYR_H__ */
