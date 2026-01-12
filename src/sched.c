
#include <rlc/sched.h>

#include "common.h"

static void item_dealloc(struct rlc_sched_item *item)
{
        if (item->dealloc != NULL) {
                item->dealloc(item);
        }
}

rlc_errno rlc_sched_init(struct rlc_sched *sched)
{
        sched->queue = NULL;
        return gabs_mutex_init(&sched->lock);
}

rlc_errno rlc_sched_deinit(struct rlc_sched *sched)
{
        return gabs_mutex_deinit(&sched->lock);
}

void rlc_sched_reset(struct rlc_sched *sched)
{
        struct rlc_sched_item *item;

        rlc_lock_acquire(&sched->lock);

        for (rlc_each_node_safe(struct rlc_sched_item, sched->queue, item,
                                next)) {
                item_dealloc(item);
        }

        sched->queue = NULL;

        rlc_lock_release(&sched->lock);
}

void rlc_sched_put(struct rlc_sched *sched, struct rlc_sched_item *item)
{
        struct rlc_sched_item *cur;
        struct rlc_sched_item **slot;

        rlc_lock_acquire(&sched->lock);

        slot = &sched->queue;

        for (rlc_each_node(*slot, cur, next)) {
                slot = &cur->next;
        }

        *slot = item;

        rlc_lock_release(&sched->lock);
}

void rlc_sched_yield(struct rlc_sched *sched)
{
        struct rlc_sched_item *item;

        rlc_lock_acquire(&sched->lock);

        for (;;) {
                item = sched->queue;
                if (item == NULL) {
                        break;
                }

                sched->queue = item->next;

                rlc_lock_release(&sched->lock);

                rlc_assert(item->fn != NULL);
                item->fn(item);

                rlc_lock_acquire(&sched->lock);
        }

        rlc_lock_release(&sched->lock);
}
