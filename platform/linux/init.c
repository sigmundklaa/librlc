
#include <rlc/plat.h>

extern void rlc_linux_timer_api_init(void);

void rlc_plat_init(void)
{
        rlc_linux_timer_api_init();
}
