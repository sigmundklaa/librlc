
#include <zephyr/kernel.h>

#include <rlc_plat.h>
#include <rlc/rlc.h>

extern int rlc_zephyr_timer_reset(rlc_context *ctx);

rlc_errno rlc_plat_init(rlc_platform *plat, rlc_context *ctx)
{
        ARG_UNUSED(plat);
        ARG_UNUSED(ctx);
        return 0;
}

rlc_errno rlc_plat_reset(rlc_platform *plat)
{
        return rlc_zephyr_timer_reset(
                CONTAINER_OF(plat, struct rlc_context, platform));
}

rlc_errno rlc_plat_deinit(rlc_platform *plat)
{
        ARG_UNUSED(plat);
        return 0;
}
