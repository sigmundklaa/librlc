
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <rlc/rlc.h>

static rlc_errno tx_request(struct rlc_context *ctx)
{
        rlc_tx_avail(ctx, 7);
        return 0;
}

static rlc_errno tx_submit(struct rlc_context *ctx, struct rlc_chunk *chunks,
                           size_t num_chunks)
{
        struct rlc_chunk *cur;

        for (rlc_each_item(chunks, cur, num_chunks)) {
                (void)printf("chunk: %zu\n", cur->size);
        }

        rlc_tx_avail(ctx, 7);

        return 0;
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
        .mem_alloc = mem_alloc,
        .mem_dealloc = mem_dealloc,
};

int main(void)
{
        rlc_errno status;
        rlc_context ctx;
        rlc_transfer transfer;
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

        status = rlc_init(&ctx, RLC_AM, 4, 128, &methods);
        assert(status == 0);

        status = rlc_send(&ctx, &transfer, chunks, 3);
        assert(status == 0);

        return 0;
}
