
#ifndef RLC_TASK_H__
#define RLC_TASK_H__

#include <rlc/rlc.h>

RLC_BEGIN_DECL

typedef void (*rlc_task_func)(struct rlc_task *);

struct rlc_task {
        rlc_task_func func;

        struct {
                uint8_t repeat: 1;
                uint8_t delayed: 1;
                uint8_t in_queue: 1;
        } flags;

        uint64_t exec_time_ms;

        struct rlc_task *next;
};

void rlc_task_init(struct rlc_task *task, rlc_task_func func);

void rlc_task_tick(struct rlc_context *ctx);

RLC_END_DECL

#endif /* RLC_TASK_H__ */
