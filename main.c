
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <rlc/rlc.h>
#include <rlc/chunks.h>

static rlc_errno tx_request(struct rlc_context *ctx)
{
        rlc_tx_avail(ctx, 16);
        return 0;
}

static rlc_errno tx_submit(struct rlc_context *ctx, struct rlc_chunk *chunks,
                           size_t num_chunks)
{
        struct rlc_chunk *cur;

        for (rlc_each_item(chunks, cur, num_chunks)) {
                (void)printf("chunk: %zu\n", cur->size);
        }

        rlc_rx_submit(ctx, chunks, num_chunks);
        rlc_tx_avail(ctx, 7);

        return 0;
}

static void event(const rlc_context *ctx, struct rlc_event *event)
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

static const struct rlc_methods methods = {
        .tx_request = tx_request,
        .tx_submit = tx_submit,
        .event = event,
        .mem_alloc = mem_alloc,
        .mem_dealloc = mem_dealloc,
};

int main(void)
{
        rlc_errno status;
        rlc_context ctx;
        rlc_sdu sdu;
        struct rlc_chunk chunks[3] = {
                {
                        .data = "First",
                        .size = 6,
                },
                {
                        .data = "Sec",
                        .size = 4,
                },
                {
                        .data = "Thir",
                        .size = 5,
                },
        };

        status = rlc_init(&ctx, RLC_UM, 4, 128, &methods);
        assert(status == 0);

        status = rlc_send(&ctx, &sdu, chunks, 3);
        assert(status == 0);

        return 0;
}
