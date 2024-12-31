
#include <string.h>

#include "task.h"
#include "utils.h"
#include "platform.h"

void rlc_task_init(struct rlc_task *task, rlc_task_func func)
{
        (void)memset(task, 0, sizeof(*task));

        task->func = func;
}

static void rlc_task_add(struct rlc_context *ctx, struct rlc_task *task)
{
        struct rlc_task *cur;
        struct rlc_task **lastp;

        lastp = &ctx->tasks;

        for (rlc_each_node(ctx->tasks, cur, next)) {
                lastp = &cur->next;
        }

        *lastp = task;

        task->flags.in_queue = 1;
}

void rlc_task_sched_periodic(struct rlc_context *ctx, struct rlc_task *task)
{
        for (rlc_plat_mx_locked(NULL)) {
        }
}

/*
 * timer reassembly
 * timer request ack
 * timer resend poll */

void rlc_task_tick(struct rlc_context *ctx)
{
        struct rlc_task *cur;
        struct rlc_task **lastp;
        uint64_t now_ms;

        lastp = &ctx->tasks;

        for (rlc_each_node(ctx->tasks, cur, next)) {
                now_ms = rlc_plat_time_ms();

                if (cur->flags.delayed && now_ms < cur->exec_time_ms) {
                        continue;
                }

                rlc_assert(cur->func != NULL);
                cur->func(cur);

                if (!cur->flags.repeat) {
                        cur->flags.in_queue = 0;

                        *lastp = cur->next;

                        continue;
                }

                lastp = &cur->next;
        }
}
