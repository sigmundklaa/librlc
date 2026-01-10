
#include <rlc/plat.h>

extern int rlc_linux_timer_manager_init(struct rlc_linux_timer_manager *man,
                                        struct rlc_context *ctx);
extern int rlc_linux_timer_manager_reset(struct rlc_linux_timer_manager *man);
extern int rlc_linux_timer_manager_deinit(struct rlc_linux_timer_manager *man);

rlc_errno rlc_plat_init(struct rlc_platform_linux *plat,
                        struct rlc_context *ctx)
{
        return rlc_linux_timer_manager_init(&plat->timer_man, ctx);
}

rlc_errno rlc_plat_reset(struct rlc_platform_linux *plat)
{
        return rlc_linux_timer_manager_reset(&plat->timer_man);
}

rlc_errno rlc_plat_deinit(struct rlc_platform_linux *plat)
{
        return rlc_linux_timer_manager_deinit(&plat->timer_man);
}
