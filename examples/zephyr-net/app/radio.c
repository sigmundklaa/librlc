
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "radio.h"

LOG_MODULE_REGISTER(radio_manager);

static void tx_timeout(struct radio_manager *manager, k_timeout_t delay);

static void tx_work(struct k_work *work)
{
        struct k_work_delayable *dwork;
        struct radio_manager *manager;
        struct net_buf *head;
        struct net_buf *cur;
        size_t remaining;
        size_t size;
        int status;

        dwork = k_work_delayable_from_work(work);
        manager = CONTAINER_OF(dwork, struct radio_manager, dwork);
        remaining = manager->radio->mtu;
        head = NULL;

        manager->tx_avail(manager, remaining);

        for (;;) {
                cur = k_fifo_peek_head(&manager->fifo);
                if (cur == NULL) {
                        break;
                }

                size = net_buf_frags_len(cur);
                if (size > remaining) {
                        break;
                }

                (void)k_fifo_get(&manager->fifo, K_NO_WAIT);

                head = net_buf_frag_add(head, cur);
                __ASSERT_NO_MSG(head != NULL);

                remaining -= size;
        }

        if (head != NULL) {
                if (remaining < manager->radio->mtu) {
                        status = manager->radio->send(manager->radio, head);
                        if (status != 0) {
                                LOG_ERR("Unable to send: %i", status);
                        }
                }

                net_buf_unref(head);
        }

        tx_timeout(manager, K_USEC(manager->radio->tx_tx_delay));
}

/* Impose a timeout on TX, if a timeout is not already running. If it is,
 * nothing is done. */
static void tx_timeout(struct radio_manager *manager, k_timeout_t delay)
{
        int status;
        k_timepoint_t tp;

        tp = sys_timepoint_calc(delay);

        status = k_work_schedule(&manager->dwork, delay);
        __ASSERT_NO_MSG(status >= 0);

        /* If the new timepoint is after the currently scheduled timepoint, then
         * extend the deadline to the new timepoint. */
        if (status == 0 && sys_timepoint_cmp(manager->deadline, tp) < 0) {
                status = k_work_reschedule(&manager->dwork, delay);
                __ASSERT_NO_MSG(status >= 0);
        }

        manager->deadline = tp;
}

void radio_tx_request(struct radio_manager *manager)
{
        tx_timeout(manager, K_TICKS(1));
}

void radio_tx_put(struct radio_manager *manager, struct net_buf *buf)
{
        buf = net_buf_ref(buf);
        k_fifo_put(&manager->fifo, buf);
}

enum net_verdict radio_rx_handle(struct radio_manager *manager,
                                 struct net_buf *buf)
{
        tx_timeout(manager, K_USEC(manager->radio->rx_tx_delay));

        return NET_CONTINUE;
}

void radio_init(struct radio_manager *manager, struct radio *radio,
                tx_avail_cb tx_avail)
{
        (void)memset(manager, 0, sizeof(*manager));

        manager->tx_avail = tx_avail;
        manager->radio = radio;
        radio->manager = manager;

        k_work_init_delayable(&manager->dwork, tx_work);
        k_fifo_init(&manager->fifo);
}
