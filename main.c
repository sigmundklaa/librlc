
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <rlc/rlc.h>
#include <rlc/chunks.h>
#include <fcntl.h>
#include <unistd.h>

static FILE *fp;
#define printf(...) fprintf(fp, ##__VA_ARGS__)

static rlc_context ctx1;
static rlc_context ctx2;

static rlc_errno p1_tx_request(struct rlc_context *ctx)
{
        rlc_tx_avail(ctx, 200);
        return 0;
}

static rlc_errno p1_tx_submit(struct rlc_context *ctx,
                              const struct rlc_chunk *chunks)
{
        const struct rlc_chunk *cur;

        static int i = 0;
        i = (i + 1) % 3;
        if (i == 2) {
                (void)printf("p1 dropping\n");
                return 0;
        }

        (void)printf("p1 Submit\n");

        for (rlc_each_node(chunks, cur, next)) {
                (void)printf("p1 chunk: %zu\n", cur->size);
        }

        rlc_rx_submit(&ctx2, chunks);
        rlc_tx_avail(&ctx2, 200);

        rlc_tx_avail(ctx, 200);

        return 0;
}

static void p1_event(rlc_context *ctx, const struct rlc_event *event)
{
        assert(0);
}

static rlc_errno p2_tx_request(struct rlc_context *ctx)
{
        rlc_tx_avail(ctx, 200);
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

        rlc_rx_submit(&ctx1, chunks);
        rlc_tx_avail(&ctx1, 200);

        rlc_tx_avail(ctx, 200);

        return 0;
}

static void p2_event(rlc_context *ctx, const struct rlc_event *event)
{
        if (event->type != RLC_EVENT_RX_DONE) {
                (void)printf("unknown %i\n", (int)event->type);
        }

        (void)printf("size: %zu\n", event->data.rx_done.size);

        char c;
        for (int i = 0; i < event->data.rx_done.size; i++) {
                c = *((char *)event->data.rx_done.data + i);
                (void)printf("%c", c);
        }
        (void)printf("\n");
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
        "neFirOneFirOneFirOneFirOneFirOne"
#define SEC_STR                                                                \
        "SecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSe" \
        "cTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecTwoSecT" \
        "woSecTwo"
#define THIRD_STR                                                              \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThree" \
        "ThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiThreeThiT" \
        "hreeThiThreeThiThree"

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

        fp = fopen("main.txt", "ww++");
        assert(fp != NULL);

#define link(l_, r_) chunks[l_].next = &chunks[r_]

        link(0, 1);
        link(1, 2);
        (void)printf("chunk size: %zu\n", rlc_chunks_size(chunks));

        status = rlc_init(&ctx1, RLC_AM, 4, 1280, &p1_methods);
        assert(status == 0);

        status = rlc_init(&ctx2, RLC_AM, 4, 1280, &p2_methods);
        assert(status == 0);

        status = rlc_send(&ctx1, chunks);
        assert(status == 0);

        return 0;
}
