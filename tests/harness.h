
#ifndef HARNESS_H__
#define HARNESS_H__

#include <rlc/rlc.h>

#include "utils.h"

static void *harness_alloc(struct rlc_context *c, size_t size,
                           enum rlc_alloc_type type)
{
        switch (type) {
        case RLC_ALLOC_BUF:
                size += sizeof(rlc_buf);
                break;
        default:
                break;
        }

        return malloc(size);
}

static void harness_dealloc(struct rlc_context *ctx, void *mem,
                            enum rlc_alloc_type type)
{
        free(mem);
}

static struct rlc_context ctx;

static void harness_setup(void)
{
        static const struct rlc_methods methods = {
                .event = NULL,
                .mem_alloc = harness_alloc,
                .mem_dealloc = harness_dealloc,
                .tx_request = NULL,
                .tx_submit = NULL,
        };
        static const struct rlc_config conf = {};

        rlc_errno status;

        status = rlc_init(&ctx, RLC_AM, &conf, &methods, NULL);
        assert(status == 0);
}

static void harness_teardown(void)
{
        rlc_errno status;

        status = rlc_deinit(&ctx);
        assert(status == 0);
}

#endif /* HARNESS_H__ */
