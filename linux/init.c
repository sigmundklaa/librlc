
#include <rlc/plat.h>

extern int rlc_linux_timer_manager_init(struct rlc_linux_timer_manager *man);
extern int rlc_linux_timer_manager_deinit(struct rlc_linux_timer_manager *man);

int rlc_plat_init(struct rlc_platform_linux *plat)
{
        return rlc_linux_timer_manager_init(&plat->timer_man);
}

int rlc_plat_deinit(struct rlc_platform_linux *plat)
{
        return rlc_linux_timer_manager_deinit(&plat->timer_man);
}
