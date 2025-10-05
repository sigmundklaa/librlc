
#ifndef L2_H__
#define L2_H__

#include <zephyr/net/net_l2.h>

#include <rlc/rlc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_RLC_AM_L2 NET_RLC_AM
NET_L2_DECLARE_PUBLIC(NET_RLC_AM_L2);

struct l2_sdu {
        rlc_errno status;
        struct rlc_sdu rlc_sdu;
        struct k_sem sem;
};

#ifdef __cpluslus
}
#endif

#endif /* L2_H__ */
