
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <rlc/rlc.h>
#include <rlc/chunks.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>

static FILE *fp;
#define printf(...) fprintf(fp, ##__VA_ARGS__)

struct ctx {
        struct rlc_context rlc;

        sem_t sig;
        sem_t lock;
        sem_t done;

        uint8_t buf[1024];
        size_t size;

        struct ctx *other;
};

static struct ctx ctx1;
static struct ctx ctx2;

static uint8_t correct_[6000];
static uint8_t received_[6000];

static void move_to_(struct ctx *c, const struct rlc_chunk *chunks)
{
        sem_wait(&c->lock);
        if (chunks != NULL) {
                c->size = rlc_chunks_deepcopy(chunks, c->buf, sizeof(c->buf));
        } else {
                c->size = 0;
        }
        sem_post(&c->sig);
}

static rlc_errno p1_tx_request(struct rlc_context *ctx)
{
        // rlc_tx_avail(ctx, 200);
        return 0;
}

static rlc_errno p1_tx_submit(struct rlc_context *ctx,
                              const struct rlc_chunk *chunks)
{
        const struct rlc_chunk *cur;
        int status;

        static int i = 0;
        uint8_t buf[1];
        if (ctx->type == RLC_AM) {
                rlc_chunks_deepcopy(chunks, buf, 1);
                if (!(buf[0] & (1 << 6))) {
                        i = (i + 1) % 11;
                        if (i == 4) {
                                (void)printf("p1 dropping\n");
                                return 0;
                        }
                }
        }

        (void)printf("p1 Submit\n");

        for (rlc_each_node(chunks, cur, next)) {
                (void)printf("p1 chunk: %zu\n", cur->size);
        }

        move_to_(&ctx2, chunks);

        return 0;
}

static void p1_event(rlc_context *ctx, const struct rlc_event *event)
{
        (void)printf("p1 event: %i\n", (int)event->type);

        if (event->type == RLC_EVENT_TX_DONE) {
                sem_post(&ctx1.done);
                sem_post(&ctx1.done);
        }
}

static void *worker(void *c_arg)
{
        struct ctx *c = c_arg;
        struct rlc_chunk chunk;

        for (;;) {
                if (sem_trywait(&c->done) == 0) {
                        break;
                } else if (sem_trywait(&c->sig) != 0) {
                        continue;
                }

                if (c->size != 0) {
                        chunk.data = c->buf;
                        chunk.size = c->size;
                        chunk.next = NULL;

                        rlc_rx_submit(&c->rlc, &chunk);
                }

                sem_post(&c->lock);
        }

        return NULL;
}

static rlc_errno p2_tx_request(struct rlc_context *ctx)
{
        // rlc_tx_avail(ctx, 200);
        return 0;
}

static rlc_errno p2_tx_submit(struct rlc_context *ctx,
                              const struct rlc_chunk *chunks)
{
        const struct rlc_chunk *cur;

        (void)printf("p2 Submit\n");

        for (rlc_each_node(chunks, cur, next)) {
                (void)printf("p2 chunk: %zu\n", cur->size);
        }

        move_to_(&ctx1, chunks);

        return 0;
}

static void p2_event(rlc_context *ctx, const struct rlc_event *event)
{
        switch (event->type) {
        case RLC_EVENT_RX_DONE:
                break;
        case RLC_EVENT_RX_FAIL:
                (void)printf("SDU dropped\n");
                return;
        default:
                (void)printf("Unknown event p2\n");
                return;
        }

        (void)printf("size: %zu\n", event->data.rx_done->size);

        char c;
        for (int i = 0; i < event->data.rx_done->size; i++) {
                c = *((char *)event->data.rx_done->data + i);
                (void)printf("%c", c);
        }
        (void)printf("\n");

        ssize_t sz = rlc_chunks_deepcopy(event->data.rx_done, received_,
                                         sizeof(received_));
        assert(sz > 0 && memcmp(correct_, received_, sz) == 0);
        (void)printf("Done\n");

        sem_post(&ctx2.done);
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

static const struct rlc_methods p1_methods = {
        .tx_request = p1_tx_request,
        .tx_submit = p1_tx_submit,
        .event = p1_event,
        .mem_alloc = mem_alloc,
        .mem_dealloc = mem_dealloc,
};

static const struct rlc_methods p2_methods = {
        .tx_request = p2_tx_request,
        .tx_submit = p2_tx_submit,
        .event = p2_event,
        .mem_alloc = mem_alloc,
        .mem_dealloc = mem_dealloc,
};

#define FIRST_STR                                                              \
        "FirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFi" \
        "rOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirO" \
        "neFirOneFirOneFirOneFirOneFirOne"                                     \
        "FirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFi" \
        "rOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirO" \
        "neFirOneFirOneFirOneFirOneFirOne"                                     \
        "FirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFi" \
        "FirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFi" \
        "FirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFi" \
        "FirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFi" \
        "rOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirO" \
        "neFirOneFirOneFirOneFirOneFirOne"                                     \
        "rOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirO" \
        "neFirOneFirOneFirOneFirOneFirOne"                                     \
        "rOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirO" \
        "neFirOneFirOneFirOneFirOneFirOne"                                     \
        "rOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirOneFirO" \
        "neFirOneFirOneFirOneFirOneFirOne"
#define SEC_STR                                                                \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"                                                             \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"                                                             \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"                                                             \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"                                                             \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"                                                             \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"                                                             \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"                                                             \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"                                                             \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"                                                             \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"
#define THIRD_STR                                                              \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"                                                 \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"

static void ctx_init(struct ctx *c)
{
        int status;

        status = sem_init(&c->sig, 0, 0);
        assert(status == 0);

        status = sem_init(&c->done, 0, 0);
        assert(status == 0);

        status = sem_init(&c->lock, 0, 1);
        assert(status == 0);
}

int main(void)
{
        rlc_errno status;
        struct rlc_chunk chunks[3] = {
                {
                        .data = FIRST_STR,
                        .size = sizeof(FIRST_STR) - 1,
                },
                {
                        .data = SEC_STR,
                        .size = sizeof(SEC_STR) - 1,
                },
                {
                        .data = THIRD_STR,
                        .size = sizeof(THIRD_STR) - 1,
                },
        };

        fp = fopen("main.txt", "w+");
        assert(fp != NULL);

#define link(l_, r_) chunks[l_].next = &chunks[r_]

        link(0, 1);
        link(1, 2);
        (void)printf("chunk size: %zu\n", rlc_chunks_size(chunks));

        ctx1.other = &ctx2;
        ctx2.other = &ctx1;

        rlc_plat_init();

        ssize_t size = rlc_chunks_deepcopy(chunks, correct_, sizeof(correct_));
        assert(size == (ssize_t)rlc_chunks_size(chunks));

        const struct rlc_config conf = {
                .window_size = 40,
                .buffer_size = 5200,
                .byte_without_poll_max = 500,
                .pdu_without_poll_max = 500,
                .sn_width = RLC_SN_12BIT,
                .time_reassembly_us = 5000,
                .time_poll_retransmit_us = 500,
        };

        ctx_init(&ctx1);
        ctx_init(&ctx2);

        pthread_t t1, t2;

        status = pthread_create(&t1, NULL, worker, &ctx1);
        assert(status == 0);

        status = pthread_create(&t2, NULL, worker, &ctx2);
        assert(status == 0);

        status = rlc_init(&ctx1.rlc, RLC_AM, &conf, &p1_methods, NULL);
        assert(status == 0);

        status = rlc_init(&ctx2.rlc, RLC_AM, &conf, &p2_methods, NULL);
        assert(status == 0);

        status = rlc_send(&ctx1.rlc, chunks);
        assert(status == 0);

        for (;;) {
                static int done1 = 0;
                static int done2 = 0;

                rlc_tx_avail(&ctx1.rlc, 100);
                rlc_tx_avail(&ctx2.rlc, 100);

                if (sem_trywait(&ctx1.done) == 0) {
                        break;
                }

                // sleep(1);
        }

        exit(0);

        pthread_join(t1, NULL);
        pthread_join(t2, NULL);

        return 0;
}
