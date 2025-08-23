
#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include <stdlib.h>

#include <rlc/rlc.h>
#include <rlc/utils.h>
#include <rlc/buf.h>

#define MTU (512)

#define PACKET_LOSS_RATE (75)

struct client {
        sem_t sem;
        pthread_t thread;

        rlc_context ctx;

        struct client *other;
};

static const struct rlc_config config = {
        .window_size = 5,
        .buffer_size = 5200,
        .byte_without_poll_max = 500,
        .pdu_without_poll_max = 3,
        .sn_width = RLC_SN_12BIT,
        .time_reassembly_us = 5000,
        .max_retx_threshhold = 5,
        .time_poll_retransmit_us = 500,
};

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
                rlc_rx_submit(&cl->other->ctx, chunks);
        } else {
                rlc_dbgf("Dropping packet");
        }

        (void)tx_request(ctx);

        return 0;
}

static void event(struct rlc_context *ctx, const struct rlc_event *event)
{
        switch (event->type) {
        case RLC_EVENT_RX_DONE:
                rlc_inff("RX done");
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

static void client_init(struct client *cl, struct client *other)
{
        rlc_errno status;

        cl->other = other;

        status = rlc_init(&cl->ctx, RLC_AM, &config, &methods, cl);
        if (status != 0) {
                rlc_panicf(status, "rlc_init");
        }

        status = sem_init(&cl->sem, 0, 0);
        if (status != 0) {
                rlc_panicf(status, "sem_init");
        }

        status = pthread_create(&cl->thread, NULL, transmitter, cl);
        if (status != 0) {
                rlc_panicf(status, "pthread_create");
        }
}

#define MESSAGE                                                                \
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "            \
        "Pellentesque in ornare risus, et rutrum magna. In aliquet sapien "    \
        "diam, ac facilisis est semper quis. Morbi elementum enim vitae "      \
        "condimentum gravida. Maecenas eget velit sollicitudin, condimentum "  \
        "ante ac, bibendum velit. Sed mollis cursus ipsum in sollicitudin. "   \
        "Sed ultricies eleifend ultrices. Cras id ex et orci ornare "          \
        "fermentum et non sapien. Mauris rutrum, lorem id pellentesque "       \
        "faucibus, enim metus pulvinar diam, sit amet sollicitudin ligula "    \
        "risus id est. Donec vestibulum mi quis fermentum bibendum. Donec "    \
        "imperdiet, arcu vel viverra congue, quam nunc egestas quam, quis "    \
        "finibus odio ex non est. In quis enim metus. Morbi gravida, metus "   \
        "vel euismod malesuada, nisl purus fermentum dui, dignissim lobortis " \
        "metus diam nec elit. Duis pulvinar fringilla tellus vitae "           \
        "fringilla. Proin rutrum, diam in malesuada varius, felis tortor "     \
        "aliquet dolor, a vehicula mauris urna sit amet nisi. Proin eu dolor " \
        "leo. Donec maximus diam ac lectus scelerisque tincidunt. Proin id "   \
        "iaculis mauris, et hendrerit enim. Mauris lacinia in lectus at "      \
        "cursus. Nulla commodo dolor arcu, sed varius sem euismod vel. "       \
        "Curabitur tempus bibendum nibh vel tristique. Cras facilisis elit "   \
        "vitae turpis feugiat, sit amet efficitur nibh condimentum. Cras "     \
        "gravida iaculis placerat. Vivamus consectetur posuere dui, eu "       \
        "interdum nunc scelerisque quis. Maecenas eu nunc consectetur, "       \
        "pretium risus id, interdum lectus. Cras tellus magna, placerat vel "  \
        "dui nec, viverra posuere fusce. "

int main(void)
{
        static struct client client1;
        static struct client client2;
        struct rlc_buf *buf;
        struct timespec spec;

        rlc_plat_init();

        srandom(time(NULL));

        client_init(&client1, &client2);
        client_init(&client2, &client1);

        buf = rlc_buf_alloc(&client1.ctx, sizeof(MESSAGE));
        if (buf == NULL) {
                rlc_panicf(ENOMEM, "main()");
        }

        (void)memcpy(buf->mem, MESSAGE, buf->size);

        spec.tv_sec = 0;
        spec.tv_nsec = 1e3;

        for (;;) {
                (void)rlc_send(&client1.ctx, buf);

                nanosleep(&spec, NULL);
        }
}
