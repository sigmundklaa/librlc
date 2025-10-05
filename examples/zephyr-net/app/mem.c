
#include <stdalign.h>

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include "l2.h"

NET_BUF_POOL_VAR_DEFINE(l2_buf_pool, 500, 50000, 0, NULL);
K_MEM_SLAB_DEFINE_STATIC(l2_sdu_pool, sizeof(struct l2_sdu), 100,
                         alignof(struct l2_sdu));
K_MEM_SLAB_DEFINE_STATIC(l2_seg_pool, sizeof(struct rlc_sdu_segment), 100,
                         alignof(struct rlc_sdu_segment));

void *l2_mem_alloc(rlc_context *ctx, size_t size, enum rlc_alloc_type type)
{
        int status;
        void *mem;
        struct l2_sdu *sdu;

        switch (type) {
        case RLC_ALLOC_BUF:
                return net_buf_alloc_len(&l2_buf_pool, size, K_NO_WAIT);
        case RLC_ALLOC_SDU:
                status = k_mem_slab_alloc(&l2_sdu_pool, (void **)&sdu,
                                          K_NO_WAIT);
                __ASSERT(status == 0, "Failed to alloc SDU");

                k_sem_init(&sdu->sem, 0, K_SEM_MAX_LIMIT);
                return &sdu->rlc_sdu;
        case RLC_ALLOC_SDU_SEGMENT:
                status = k_mem_slab_alloc(&l2_seg_pool, &mem, K_NO_WAIT);
                __ASSERT(status == 0, "Failed to alloc SDU segment");
                return mem;
        }

        CODE_UNREACHABLE;
}

void l2_mem_dealloc(rlc_context *ctx, void *mem, enum rlc_alloc_type type)
{
        struct l2_sdu *sdu;

        switch (type) {
        case RLC_ALLOC_BUF:
                __ASSERT_NO_MSG(0);
                break;
        case RLC_ALLOC_SDU:
                sdu = CONTAINER_OF(mem, struct l2_sdu, rlc_sdu);

                k_mem_slab_free(&l2_sdu_pool, sdu);
                break;
        case RLC_ALLOC_SDU_SEGMENT:
                k_mem_slab_free(&l2_seg_pool, mem);
                break;
        }
}
