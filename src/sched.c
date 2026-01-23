
#include <rlc/sched.h>

#include "common.h"

static struct rlc_sched_item *from_it(rlc_list_it it)
{
        return rlc_list_it_item(it, struct rlc_sched_item, list_node);
}

static void item_dealloc(struct rlc_sched_item *item)
{
        if (item->dealloc != NULL) {
                item->dealloc(item);
        }
}

rlc_errno rlc_sched_init(struct rlc_sched *sched)
{
        rlc_list_init(&sched->queue);
        return gabs_mutex_init(&sched->lock);
}

rlc_errno rlc_sched_deinit(struct rlc_sched *sched)
{
        return gabs_mutex_deinit(&sched->lock);
}

void rlc_sched_reset(struct rlc_sched *sched)
{
        struct rlc_sched_item *item;
        rlc_list_it it;

        rlc_lock_acquire(&sched->lock);

        rlc_list_foreach_safe(&sched->queue, it)
        {
                item = from_it(it);
                item_dealloc(item);
        }

        rlc_list_init(&sched->queue);
        rlc_lock_release(&sched->lock);
}

void rlc_sched_put(struct rlc_sched *sched, struct rlc_sched_item *item)
{
        rlc_list_it it;

        rlc_lock_acquire(&sched->lock);

        rlc_list_foreach(&sched->queue, it){}

        (void)rlc_list_it_put_back(it, &item->list_node);

        rlc_lock_release(&sched->lock);
}

void rlc_sched_yield(struct rlc_sched *sched)
{
        struct rlc_sched_item *item;
        rlc_list_it it;

        rlc_lock_acquire(&sched->lock);

        for (;;) {
                /* Always get head, as we don't want to depend on iteration
                 * when multiple threads may remove from the queue. */
                it = rlc_list_it_init(&sched->queue);
                if (rlc_list_it_node(it) == NULL) {
                        break;
                }

                item = from_it(it);
                (void)rlc_list_it_pop(it, NULL);

                rlc_lock_release(&sched->lock);

                rlc_assert(item->fn != NULL);
                item->fn(item);

                rlc_lock_acquire(&sched->lock);
        }

        rlc_lock_release(&sched->lock);
}
