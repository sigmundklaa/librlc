
#ifndef RADIO_H__
#define RADIO_H__

#include <zephyr/net_buf.h>
#include <zephyr/net/net_pkt.h>

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct radio {
        int (*send)(struct radio *, struct net_buf *);

        uint64_t tx_tx_delay;
        uint64_t rx_tx_delay;
        size_t mtu;

        struct radio_manager *manager;
};

struct radio_manager;
typedef void (*tx_avail_cb)(struct radio_manager *, size_t);

struct radio_manager {
        struct radio *radio;
        struct k_work_delayable dwork;
        k_timepoint_t deadline;

        struct k_fifo fifo;

        tx_avail_cb tx_avail;
};

void radio_tx_request(struct radio_manager *manager);

void radio_tx_put(struct radio_manager *manager, struct net_buf *buf);

enum net_verdict radio_rx_handle(struct radio_manager *manager,
                                 struct net_buf *buf);

void radio_init(struct radio_manager *manager, struct radio *radio,
                tx_avail_cb tx_avail);

#ifdef __cplusplus
}
#endif

#endif /* RADIO_H__ */
