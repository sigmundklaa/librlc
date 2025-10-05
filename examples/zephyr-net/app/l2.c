
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/logging/log.h>

#include <rlc/rlc.h>
#include <rlc/sdu.h>

#include "l2.h"
#include "radio.h"
#include "mem.h"

LOG_MODULE_REGISTER(l2);

struct rlc_ctx_container {
        sys_dnode_t node;
        rlc_context ctx;
};

struct rlc_l2_ctx {
        sys_dlist_t ctxs;
        struct radio_manager manager;
};

static struct rlc_l2_ctx l2_ctx;

/* Invoke on all network interfaces with link address */
static void l2_tx_avail(struct radio_manager *manager, size_t size)
{
        struct rlc_ctx_container *container;

        SYS_DLIST_FOR_EACH_CONTAINER(&l2_ctx.ctxs, container, node)
        {
                if (size == 0) {
                        break;
                }

                size = rlc_tx_avail(&container->ctx, size);
        }
}

static rlc_errno l2_tx_submit(rlc_context *ctx, rlc_buf *buf)
{
        radio_tx_put(&l2_ctx.manager, buf);
        return 0;
}

static rlc_errno l2_tx_request(rlc_context *ctx)
{
        radio_tx_request(&l2_ctx.manager);
        return 0;
}

static void rx_buf(struct net_if *iface, struct net_buf *buf)
{
        struct net_pkt *pkt;
        int status;

        pkt = net_pkt_rx_alloc(K_NO_WAIT);
        if (pkt == NULL) {
                __ASSERT_NO_MSG(0);
                return;
        }

        net_pkt_frag_add(pkt, net_buf_ref(buf));

        status = net_recv_data(iface, pkt);
        if (status != 0) {
                LOG_ERR("Buffer reception failed: %i", status);
        }
}

static void l2_event(rlc_context *ctx, const struct rlc_event *event)
{
        struct l2_sdu *container;
        struct net_if *iface;

        if (event->type != RLC_EVENT_RX_DONE_DIRECT) {
                container = CONTAINER_OF(event->sdu, struct l2_sdu, rlc_sdu);
        }

        iface = rlc_user_data(ctx);

        switch (event->type) {
        case RLC_EVENT_RX_DONE_DIRECT:
                rx_buf(iface, event->buf);
                break;
        case RLC_EVENT_RX_DONE:
                rx_buf(iface, event->sdu->buffer);
                break;
        case RLC_EVENT_RX_FAIL:
                __ASSERT(0, "RX failure");
                break;
        case RLC_EVENT_TX_DONE:
                container->status = 0;
                k_sem_give(&container->sem);
                __ASSERT(0, "TX failure");
                break;
        case RLC_EVENT_TX_FAIL:
                container->status = -ECONNABORTED;
                k_sem_give(&container->sem);
                break;
        }
}

static const struct rlc_methods l2_rlc_methods = {
        .tx_submit = l2_tx_submit,
        .tx_request = l2_tx_request,
        .event = l2_event,
        .mem_alloc = l2_mem_alloc,
        .mem_dealloc = l2_mem_dealloc,
};

static enum net_verdict l2_recv(struct net_if *iface, struct net_pkt *pkt)
{
        rlc_rx_submit(net_if_l2_data(iface), pkt->buffer);
        return NET_OK;
}

static int l2_send(struct net_if *iface, struct net_pkt *pkt)
{
        /*  create buffer, rlc_send */
        struct rlc_sdu *sdu;
        struct l2_sdu *container;
        rlc_context *ctx;
        rlc_errno status;

        ctx = net_if_l2_data(iface);

        status = rlc_send(NULL, pkt->buffer, &sdu);
        if (status != 0) {
                return status;
        }

        container = CONTAINER_OF(sdu, struct l2_sdu, rlc_sdu);
        k_sem_take(&container->sem, K_FOREVER);

        rlc_sdu_decref(ctx, sdu);

        return 0;
}

static const struct rlc_config l2_config = {
        .buffer_size = 1500,
        .window_size = 5,
        .byte_without_poll_max = 500,
        .pdu_without_poll_max = 5,
        .time_poll_retransmit_us = 100e3,
        .time_reassembly_us = 500e3,
        .sn_width = RLC_SN_18BIT,
        .max_retx_threshhold = 5,
};

static int l2_enable(struct net_if *iface, bool state)
{
        rlc_context *ctx;

        ctx = net_if_l2_data(iface);
        if (!state) {
                LOG_INF("Disabling RLC context");
                return rlc_deinit(ctx);
        }

        LOG_INF("Enabling RLC context");
        return rlc_init(ctx, RLC_AM, &l2_config, &l2_rlc_methods, iface);
}

static enum net_l2_flags l2_get_flags(struct net_if *iface)
{
        return 0;
}

static int rlc_l2_init(void)
{
        radio_init(&l2_ctx.manager, l2_tx_avail);
        return 0;
}

SYS_INIT(rlc_l2_init, POST_KERNEL, 99);
NET_L2_INIT(NET_RLC_AM_L2, l2_recv, l2_send, l2_enable, l2_get_flags);
