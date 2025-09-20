
#include <zephyr/kernel.h>
#include <zephyr/sys/bitarray.h>

#include <rlc/timer.h>

#define CONFIG_RLC_TIMER_MAX (100)

struct timer_info {
        enum {
                STATUS_STOPPED,
                STATUS_RUNNING,
        } status;

        struct k_timer timer;

        rlc_timer_cb callback;
        struct rlc_context *ctx;
};

static struct timer_info pool[CONFIG_RLC_TIMER_MAX];
SYS_BITARRAY_DEFINE_STATIC(pool_bitarray, CONFIG_RLC_TIMER_MAX);

static size_t to_idx(struct timer_info *info)
{
        __ASSERT_NO_MSG(info >= pool);
        return info - pool;
}

static struct timer_info *reserve(void)
{
        int status;
        size_t idx;

        status = sys_bitarray_alloc(&pool_bitarray, 1, &idx);
        __ASSERT(status == 0, "Timer allocation failed.");

        return &pool[idx];
}

static void release(struct timer_info *info)
{
        int status;

        status = sys_bitarray_free(&pool_bitarray, 1, to_idx(info));
        __ASSERT_NO_MSG(status == 0);
}

static void timer_expiry(struct k_timer *timer)
{
        struct timer_info *info;

        info = CONTAINER_OF(timer, struct timer_info, timer);

        info->callback(info, info->ctx);
        info->status = STATUS_STOPPED;
}

static void timer_stop(struct k_timer *timer)
{
        struct timer_info *info;

        info = CONTAINER_OF(timer, struct timer_info, timer);
        info->status = STATUS_STOPPED;
}

bool rlc_plat_timer_okay(rlc_timer timer_arg)
{
        return true;
}

rlc_timer rlc_plat_timer_install(rlc_timer_cb cb, struct rlc_context *ctx)
{
        struct timer_info *info;

        info = reserve();
        info->callback = cb;
        info->status = STATUS_STOPPED;
        info->ctx = ctx;

        k_timer_init(&info->timer, timer_expiry, timer_stop);

        return info;
}

rlc_errno rlc_plat_timer_uninstall(rlc_timer timer)
{
        struct timer_info *info = timer;

        k_timer_stop(&info->timer);
        release(info);

        return 0;
}

rlc_errno rlc_plat_timer_start(rlc_timer timer, uint32_t delay_us)
{
        struct timer_info *info = timer;

        info->status = STATUS_RUNNING;

        k_timer_start(&info->timer, K_USEC(delay_us), K_NO_WAIT);
        return 0;
}

rlc_errno rlc_plat_timer_restart(rlc_timer timer, uint32_t delay_us)
{
        (void)rlc_plat_timer_stop(timer);
        (void)rlc_plat_timer_start(timer, delay_us);
        return 0;
}

rlc_errno rlc_plat_timer_stop(rlc_timer timer)
{
        struct timer_info *info = timer;

        k_timer_stop(&info->timer);

        return 0;
}

bool rlc_plat_timer_active(rlc_timer timer)
{
        struct timer_info *info = timer;

        return info->status != STATUS_STOPPED;
}
