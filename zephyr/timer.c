
#include <zephyr/kernel.h>
#include <zephyr/sys/bitarray.h>

#include <rlc/timer.h>

#define CONFIG_RLC_TIMER_MAX (100)

struct timer_info {
        struct k_work_delayable dwork;
        struct k_work_sync sync;

        unsigned int flags;
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

static void timer_expiry(struct k_work *work)
{
        struct timer_info *info;
        struct k_work_delayable *dwork;

        dwork = k_work_delayable_from_work(work);
        info = CONTAINER_OF(dwork, struct timer_info, dwork);

        info->callback(info, info->ctx);

        if (info->flags & RLC_TIMER_SINGLE) {
                release(info);
        }
}

bool rlc_plat_timer_okay(rlc_timer timer_arg)
{
        return true;
}

rlc_timer rlc_plat_timer_install(rlc_timer_cb cb, struct rlc_context *ctx,
                                 unsigned int flags)
{
        struct timer_info *info;

        info = reserve();
        info->callback = cb;
        info->ctx = ctx;
        info->flags = flags;

        k_work_init_delayable(&info->dwork, timer_expiry);

        return info;
}

rlc_errno rlc_plat_timer_uninstall(rlc_timer timer)
{
        struct timer_info *info = timer;

        (void)k_work_cancel_delayable_sync(&info->dwork, &info->sync);
        release(info);

        return 0;
}

rlc_errno rlc_plat_timer_start(rlc_timer timer, uint32_t delay_us)
{
        struct timer_info *info = timer;
        int status;
        k_timeout_t timeout;

        if (delay_us == 0) {
                timeout = K_TICKS(1);
        } else {
                timeout = K_USEC(delay_us);
        }

        status = k_work_schedule(&info->dwork, timeout);
        __ASSERT_NO_MSG(status >= 0);

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

        (void)k_work_cancel_delayable(&info->dwork);

        return 0;
}

bool rlc_plat_timer_active(rlc_timer timer)
{
        struct timer_info *info = timer;

        return k_work_delayable_busy_get(&info->dwork);
}

unsigned int rlc_plat_timer_flags(rlc_timer timer)
{
        struct timer_info *info = timer;
        return info->flags;
}
