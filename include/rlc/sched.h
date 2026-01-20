
#ifndef RLC_SCHED_H__
#define RLC_SCHED_H__

#include <gabs/mutex.h>

#include <rlc/decl.h>
#include <rlc/errno.h>
#include <rlc/list.h>

RLC_BEGIN_DECL

struct rlc_sched_item;

typedef void (*rlc_sched_item_fn)(struct rlc_sched_item *);
typedef void (*rlc_sched_item_dealloc)(struct rlc_sched_item *);

struct rlc_sched_item {
        rlc_sched_item_fn fn;
        rlc_sched_item_dealloc dealloc;

        rlc_list_node list_node;
};

struct rlc_sched {
        rlc_list queue;
        gabs_mutex lock;
};

static inline void rlc_sched_item_init(struct rlc_sched_item *item,
                                       rlc_sched_item_fn fn,
                                       rlc_sched_item_dealloc dealloc)
{
        item->fn = fn;
        item->dealloc = dealloc;

        rlc_list_node_init(&item->list_node);
}

rlc_errno rlc_sched_init(struct rlc_sched *sched);

rlc_errno rlc_sched_deinit(struct rlc_sched *sched);

void rlc_sched_reset(struct rlc_sched *sched);

void rlc_sched_put(struct rlc_sched *sched, struct rlc_sched_item *item);

void rlc_sched_yield(struct rlc_sched *sched);

RLC_END_DECL

#endif /* RLC_SCHED_H__ */
