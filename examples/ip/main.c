
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include <semaphore.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include <rlc/rlc.h>
#include <rlc/chunks.h>
#include <rlc/utils.h>

#define MTU (50)

#define PACKET_LOSS_RATE (50)

struct queue {
        size_t cap;
        size_t length;
        size_t index;
        uintptr_t mem;

        size_t elem_size;
        size_t elem_align;

        pthread_mutex_t lock;
        sem_t non_full;
        sem_t non_empty;
};

static size_t aligned_(size_t sz, size_t align)
{
        return (sz + (align - 1)) & ~(align - 1);
}

struct q_entry {
        uint8_t mem[MTU];
        size_t size;
};

static void q_init(struct queue *q, size_t cap, size_t elem_size,
                   size_t elem_align)
{
        int status;

        q->mem = (uintptr_t)malloc(aligned_(elem_size, elem_align) * cap);
        q->index = 0;
        q->cap = cap;
        q->length = 0;
        q->elem_size = elem_size;
        q->elem_align = elem_align;

        status = sem_init(&q->non_full, 0, 1);
        assert(status == 0);

        status = sem_init(&q->non_empty, 0, 0);
        assert(status == 0);

        status = pthread_mutex_init(&q->lock, NULL);
        assert(status == 0);
}

static void q_push(struct queue *q, void *src)
{
        uintptr_t addr;
        uintptr_t idx;

        (void)sem_wait(&q->non_full);
        (void)pthread_mutex_lock(&q->lock);

        idx = (q->index + q->length) % q->cap;
        addr = q->mem + idx * q->elem_size;

        (void)memcpy((void *)addr, src, q->elem_size);

        q->length++;

        (void)sem_post(&q->non_empty);
        (void)pthread_mutex_unlock(&q->lock);
}

static void q_pop(struct queue *q, void *mem)
{
        uintptr_t addr;

        (void)sem_wait(&q->non_empty);
        (void)pthread_mutex_lock(&q->lock);

        addr = q->mem + q->index * q->elem_size;

        q->index = (q->index + 1) % q->cap;
        q->length--;

        (void)memcpy(mem, (void *)addr, q->elem_size);

        (void)sem_post(&q->non_full);
        (void)pthread_mutex_unlock(&q->lock);
}

static void q_push_chunk(struct queue *q, const struct rlc_chunk *chunks)
{
        struct q_entry entry;
        ssize_t size;

        size = rlc_chunks_deepcopy(chunks, entry.mem, sizeof(entry.mem));
        assert(size >= 0);

        entry.size = size;

        q_push(q, &entry);
}

static struct rlc_chunk q_pop_chunk(struct queue *q, void *mem)
{
        struct q_entry entry;

        q_pop(q, &entry);
        (void)memcpy(mem, &entry.mem, entry.size);

        return (struct rlc_chunk){
                .data = mem,
                .size = entry.size,
                .next = NULL,
        };
}

struct client {
        sem_t sem;
        pthread_t tx_thread;

        pthread_t rx_thread;
        int tun_fd;

        rlc_context ctx;

        struct client *other;

        struct queue q;
        pthread_t q_thread;
};

static const struct rlc_config config = {
        .window_size = 5,
        .buffer_size = 5200,
        .byte_without_poll_max = 500,
        .pdu_without_poll_max = 3,
        .sn_width = RLC_SN_12BIT,
        .time_reassembly_us = 5000,
        .time_poll_retransmit_us = 10000,
};

static void *q_worker(void *client_arg)
{
        struct client *cl;
        struct rlc_chunk chunk;
        uint8_t buf[MTU];

        cl = client_arg;

        for (;;) {
                chunk = q_pop_chunk(&cl->q, buf);

                rlc_rx_submit(&cl->ctx, &chunk);
        }
}

static void *transmitter(void *client_arg)
{
        rlc_errno status;
        struct client *cl;

        cl = client_arg;

        for (;;) {
                status = sem_wait(&cl->sem);
                if (status != 0) {
                        rlc_errf("sem_wait: %" RLC_PRI_ERRNO, status);
                        continue;
                }

                rlc_tx_avail(&cl->ctx, MTU);
        }

        return NULL;
}

static void *tun_receiver(void *client_arg)
{
        rlc_errno status;
        struct client *cl;
        int bytes;
        uint8_t buf[1500];
        struct rlc_chunk chunk;

        cl = client_arg;
        chunk.next = NULL;

        for (;;) {
                bytes = read(cl->tun_fd, buf, sizeof(buf));
                (void)printf("Received %i bytes\n", bytes);
                if (bytes <= 0) {
                        rlc_errf("read: %i", errno);
                        continue;
                }

                chunk.data = buf;
                chunk.size = bytes;

                status = rlc_send(&cl->ctx, &chunk);
                if (status != 0) {
                        rlc_errf("rlc_send: %" RLC_PRI_ERRNO, status);
                        continue;
                }
        }

        return NULL;
}

static rlc_errno tx_request(struct rlc_context *ctx)
{
        struct client *cl = rlc_user_data(ctx);

        (void)sem_post(&cl->sem);
        return 0;
}

static rlc_errno tx_submit(struct rlc_context *ctx,
                           const struct rlc_chunk *chunks)
{
        struct client *cl = rlc_user_data(ctx);

        if (random() % 100 >= PACKET_LOSS_RATE) {
                q_push_chunk(&cl->other->q, chunks);
        } else {
                rlc_wrnf("Dropping packet");
        }

        (void)tx_request(ctx);

        return 0;
}

static void tun_write(struct client *cl, const struct rlc_chunk *chunks)
{
        uint8_t buf[rlc_chunks_size(chunks)];
        ssize_t bytes;

        bytes = rlc_chunks_deepcopy(chunks, buf, sizeof(buf));
        rlc_assert(bytes == sizeof(buf));

        bytes = write(cl->tun_fd, buf, bytes);
        rlc_assert(bytes == sizeof(buf));
}

static void event(struct rlc_context *ctx, const struct rlc_event *event)
{
        switch (event->type) {
        case RLC_EVENT_RX_DONE:
                rlc_inff("RX done");
                tun_write(rlc_user_data(ctx), event->data.rx_done);
                return;
        case RLC_EVENT_TX_DONE:
                rlc_inff("TX done");
                return;
        case RLC_EVENT_RX_FAIL:
                rlc_errf("RX failure");
                return;
        }
}

static void *mem_alloc(struct rlc_context *ctx, size_t size)
{
        (void)ctx;
        return malloc(size);
}

static void mem_dealloc(struct rlc_context *ctx, void *mem)
{
        (void)ctx;
        free(mem);
}

static const struct rlc_methods methods = {
        .tx_request = tx_request,
        .tx_submit = tx_submit,
        .event = event,
        .mem_alloc = mem_alloc,
        .mem_dealloc = mem_dealloc,
};

static void tun_init(struct client *cl, const char *tun)
{
        rlc_errno status;
        struct ifreq ifr;

        cl->tun_fd = open("/dev/net/tun", O_RDWR);
        if (cl->tun_fd < 0) {
                rlc_panicf(-errno, "open /dev/net/tun");
        }

        (void)memset(&ifr, 0, sizeof(ifr));

        ifr.ifr_flags = IFF_TUN;
        strncpy(ifr.ifr_name, tun, IFNAMSIZ);

        if (ioctl(cl->tun_fd, TUNSETIFF, &ifr) < 0) {
                rlc_panicf(-errno, "ioctl %s", tun);
        }

        status = pthread_create(&cl->rx_thread, NULL, tun_receiver, cl);
        if (status != 0) {
                rlc_panicf(status, "pthread_create");
        }
}

static void client_init(struct client *cl, const char *tun,
                        struct client *other)
{
        rlc_errno status;

        cl->other = other;

        q_init(&cl->q, 20, sizeof(struct q_entry), _Alignof(struct q_entry));

        status = rlc_init(&cl->ctx, RLC_AM, &config, &methods, cl);
        if (status != 0) {
                rlc_panicf(status, "rlc_init");
        }

        status = sem_init(&cl->sem, 0, 0);
        if (status != 0) {
                rlc_panicf(status, "sem_init");
        }

        status = pthread_create(&cl->tx_thread, NULL, transmitter, cl);
        if (status != 0) {
                rlc_panicf(status, "pthread_create");
        }

        status = pthread_create(&cl->q_thread, NULL, q_worker, cl);
        if (status != 0) {
                rlc_panicf(status, "pthread_create");
        }

        tun_init(cl, tun);
}

int main(int argc, char **argv)
{
        static struct client client1;
        static struct client client2;
        struct rlc_chunk chunk;

        if (argc < 3) {
                (void)printf("Usage: %s <tun0> <tun1>\n", argv[0]);
                exit(1);
        }

        rlc_plat_init();

        srandom(time(NULL));

        client_init(&client1, argv[1], &client2);
        client_init(&client2, argv[2], &client1);

        for (;;) {
        }
}
