
#ifndef HARNESS_H__
#define HARNESS_H__

#include <string.h>

#include <rlc/rlc.h>

#include "utils.h"

static void *harness_alloc(struct rlc_context *c, size_t size,
                           enum rlc_alloc_type type)
{
        void *ret = malloc(size);
        if (ret != NULL) {
                (void)memset(ret, 0, size);
        }

        return ret;
}

static void harness_dealloc(struct rlc_context *ctx, void *mem,
                            enum rlc_alloc_type type)
{
        free(mem);
}

static void harness_setup(struct rlc_context *ctx, enum rlc_sdu_type type)
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

        status = rlc_init(ctx, type, &conf, &methods, NULL);
        assert(status == 0);
}

static void harness_teardown(struct rlc_context *ctx)
{
        rlc_errno status;

        status = rlc_deinit(ctx);
        assert(status == 0);
}

#endif /* HARNESS_H__ */
