
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/zvfs/eventfd.h>

#include <rlc/rlc.h>
#include <rlc/buf.h>

#include "radio.h"
#include "mem.h"

LOG_MODULE_REGISTER(main);

#ifndef CONFIG_RADIO_SOCK_THREAD_STACK_SIZE
#define CONFIG_RADIO_SOCK_THREAD_STACK_SIZE (2048)
#endif /* CONFIG_RADIO_SOCK_THREAD_STACK_SIZE */

struct sock_ctx {
        int fd;
        struct rlc_context rlc;
        struct radio_manager radio;

        struct k_fifo tx_queue;
        int tx_event;
};

static int sock_connect(const char *file, struct sock_ctx *ctx)
{
        int sock;
        struct sockaddr_un addr;

        sock = zsock_socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
                return -errno;
        }

        addr.sun_family = AF_UNIX;
        (void)strcpy(addr.sun_path, file);

        if (zsock_connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                return -errno;
        }

        k_fifo_init(&ctx->tx_queue);
        ctx->tx_event = zvfs_eventfd(0, ZVFS_EFD_SEMAPHORE);

        ctx->fd = sock;
        return 0;
}

static void sock_close(struct sock_ctx *ctx)
{
        (void)zsock_close(ctx->fd);
}

static rlc_errno tx_request(struct rlc_context *ctx)
{
        struct sock_ctx *parent = CONTAINER_OF(ctx, struct sock_ctx, rlc);
        radio_tx_request(&parent->radio);

        return 0;
}

static rlc_errno tx_submit(struct rlc_context *ctx, rlc_buf *buf)
{
        struct sock_ctx *parent = CONTAINER_OF(ctx, struct sock_ctx, rlc);
        struct net_buf *header;
        uint16_t len;

        header = rlc_buf_alloc(ctx, sizeof(len));
        if (header == NULL) {
                return -ENOMEM;
        }

        len = rlc_buf_size(buf);
        rlc_buf_put(header, &len, sizeof(len));

        rlc_buf_incref(buf);
        buf = rlc_buf_chain_front(buf, header);

        LOG_INF("sending %" PRIu16, len);

        radio_tx_put(&parent->radio, buf);

        rlc_buf_decref(buf, ctx);

        return 0;
}

static void event(struct rlc_context *ctx, const struct rlc_event *event)
{
        switch (event->type) {
        case RLC_EVENT_RX_DONE_DIRECT:
                LOG_INF("rx done direct");
                break;
        case RLC_EVENT_RX_DONE:
                LOG_INF("rx done");
                break;
        case RLC_EVENT_RX_FAIL:
                LOG_ERR("rx fail");
                break;
        case RLC_EVENT_TX_DONE:
                LOG_INF("tx done");
                break;
        case RLC_EVENT_TX_FAIL:
                LOG_ERR("tx fail");
                break;
        }
}

static void tx_avail(struct radio_manager *radio, size_t size)
{
        struct sock_ctx *parent = CONTAINER_OF(radio, struct sock_ctx, radio);

        if (size < sizeof(uint16_t)) {
                return;
        }

        rlc_tx_avail(&parent->rlc, size - sizeof(uint16_t));
}

static int tx_send(struct radio *radio, struct net_buf *buf)
{
        struct sock_ctx *ctx;

        ctx = CONTAINER_OF(radio->manager, struct sock_ctx, radio);

        k_fifo_put(&ctx->tx_queue, buf);

        if (zvfs_eventfd_write(ctx->tx_event, 1) != 0) {
                LOG_ERR("Failed to write to eventfd: %i", errno);
        }

        return 0;
}

static const struct rlc_methods methods = {
        .tx_request = tx_request,
        .tx_submit = tx_submit,
        .event = event,
        .mem_alloc = l2_mem_alloc,
        .mem_dealloc = l2_mem_dealloc,
};

static void sock_send(struct sock_ctx *sock, struct net_buf *buf)
{
        size_t bytes;
        size_t ret;

        for (; buf != NULL; buf = buf->frags) {
                bytes = 0;

                LOG_DBG("Sending buffer of: %zu bytes", (size_t)buf->len);

                do {
                        ret = zsock_send(sock->fd, buf->data + bytes,
                                         buf->len - bytes, 0);
                        if (ret < 0) {
                                LOG_ERR("Failed to send: %i", errno);
                                return;
                        }

                        bytes += ret;
                } while (ret < buf->len);
        }
}

static void sock_recv(struct sock_ctx *sock)
{
        size_t ret;
        uint16_t len;
        struct net_buf *buf;

        ret = zsock_recv(sock->fd, &len, sizeof(len), 0);
        if (ret < sizeof(len)) {
                rlc_panicf(errno, "Recv header");
        }

        buf = rlc_buf_alloc(&sock->rlc, len);
        if (buf == NULL) {
                rlc_panicf(ENOMEM, "Buffer alloc");
        }

        ret = zsock_recv(sock->fd, buf->data, len, 0);
        if (ret < len) {
                rlc_panicf(errno, "Recv");
        }

        LOG_DBG("Received buffer of size %zu", ret);

        net_buf_add(buf, ret);

        (void)radio_rx_handle(&sock->radio, buf);
        buf = rlc_rx_submit(&sock->rlc, buf);

        rlc_buf_decref(buf, &sock->rlc);
}

static void sock_thread(void *ctx_arg, void *p2, void *p3)
{
        struct sock_ctx *ctx = ctx_arg;
        struct zvfs_pollfd pfds[2];
        int count;
        zvfs_eventfd_t tmp;

        LOG_INF("sock thread started");

        pfds[0].fd = ctx->fd;
        pfds[0].events = ZVFS_POLLIN;
        pfds[1].fd = ctx->tx_event;
        pfds[1].events = ZVFS_POLLIN;

        for (;;) {
                count = zvfs_poll(pfds, ARRAY_SIZE(pfds), -1);
                if (count <= 0) {
                        if (count < 0) {
                                LOG_ERR("error occured: %i", errno);
                        }

                        continue;
                }

                if (pfds[0].revents & ZVFS_POLLIN) {
                        sock_recv(ctx);
                }

                if (pfds[1].revents & ZVFS_POLLIN) {
                        (void)zvfs_eventfd_read(ctx->tx_event, &tmp);
                        sock_send(ctx, k_fifo_get(&ctx->tx_queue, K_NO_WAIT));
                }
        }
}

K_THREAD_STACK_DEFINE(sock_thread_stack, CONFIG_RADIO_SOCK_THREAD_STACK_SIZE);

static const struct rlc_config conf = {
        .buffer_size = 1500,
        .window_size = 1,
        .byte_without_poll_max = 500,
        .pdu_without_poll_max = 5,
        .time_poll_retransmit_us = 2500e3,
        .time_reassembly_us = 50000e3,
        .max_retx_threshhold = 5,
        .sn_width = RLC_SN_18BIT,
};

int main(void)
{
        struct sock_ctx ctx;
        struct k_thread thread;
        struct radio radio;
        rlc_buf *buf;
        int status;
        k_tid_t tid;

        LOG_INF("connecting");
        status = sock_connect("test.sock", &ctx);
        if (status != 0) {
                LOG_ERR("Unable to open socket: %i", status);
                return status;
        }

        LOG_INF("rlc init");
        status = rlc_init(&ctx.rlc, RLC_AM, &conf, &methods, NULL);
        if (status != 0) {
                LOG_ERR("init fail");
                return status;
        }

        radio.send = tx_send;
        radio.mtu = 256;
        radio.rx_tx_delay = 10e3;
        radio.tx_tx_delay = 30e3;

        LOG_INF("radio init");
        radio_init(&ctx.radio, &radio, tx_avail);

        tid = k_thread_create(&thread, sock_thread_stack,
                              CONFIG_RADIO_SOCK_THREAD_STACK_SIZE, sock_thread,
                              &ctx, NULL, NULL, 0, 0, K_NO_WAIT);

        for (;;) {
                buf = rlc_buf_alloc(&ctx.rlc, conf.buffer_size);
                rlc_assert(buf != NULL);

                buf->len = buf->size;

                (void)rlc_send(&ctx.rlc, buf, NULL);

                rlc_buf_decref(buf, &ctx.rlc);

                k_sleep(K_MSEC(100));
        }

        k_thread_join(tid, K_FOREVER);
        sock_close(&ctx);

        return 0;
}
