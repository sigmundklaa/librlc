
#include <rlc/buf.h>
#include <rlc/sdu.h>

#include <string.h>

#include "methods.h"
#include "log.h"

size_t rlc_sdu_count(struct rlc_context *ctx, enum rlc_sdu_dir dir)
{
        size_t count;
        struct rlc_sdu *sdu;

        count = 0;
        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir == dir) {
                        count++;
                }
        }

        return count;
}

void rlc_sdu_insert(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        struct rlc_sdu **lastp;

        lastp = &ctx->sdus;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (sdu->dir == cur->dir && sdu->sn <= cur->sn) {
                        assert(sdu->sn != cur->sn);

                        sdu->next = cur;
                        break;
                }

                lastp = &cur->next;
        }

        *lastp = sdu;
}

void rlc_sdu_remove(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        struct rlc_sdu **lastp;

        lastp = &ctx->sdus;

        for (rlc_each_node(ctx->sdus, cur, next)) {
                if (cur == sdu) {
                        *lastp = sdu->next;

                        sdu->next = NULL;
                        break;
                }

                lastp = &cur->next;
        }
}

struct rlc_sdu *rlc_sdu_alloc(struct rlc_context *ctx, enum rlc_sdu_dir dir)
{
        struct rlc_sdu *sdu;

        sdu = rlc_alloc(ctx, sizeof(*sdu), RLC_ALLOC_SDU);
        if (sdu == NULL) {
                return NULL;
        }

        (void)memset(sdu, 0, sizeof(*sdu));

        sdu->dir = dir;
        sdu->refcount = 1;

        if (sdu->dir == RLC_TX) {
                rlc_sem_init(&sdu->tx_sem, 0);
        }

        return sdu;
}

void rlc_sdu_incref(struct rlc_sdu *sdu)
{
        sdu->refcount++;
}

void rlc_sdu_decref(struct rlc_context *ctx, struct rlc_sdu *sdu)
{
        struct rlc_sdu_segment *seg;

        if (--sdu->refcount == 0) {
                rlc_buf_decref(sdu->buffer, ctx);

                for (rlc_each_node_safe(struct rlc_sdu_segment, sdu->segments,
                                        seg, next)) {
                        rlc_dealloc(ctx, seg, RLC_ALLOC_SDU_SEGMENT);
                }

                rlc_sem_deinit(&sdu->tx_sem);
                rlc_dealloc(ctx, sdu, RLC_ALLOC_SDU);
        }
}

struct rlc_sdu *rlc_sdu_get(struct rlc_context *ctx, uint32_t sn,
                            enum rlc_sdu_dir dir)
{
        struct rlc_sdu *sdu;

        for (rlc_each_node(ctx->sdus, sdu, next)) {
                if (sdu->dir == dir && sdu->sn == sn) {
                        return sdu;
                }
        }

        return NULL;
}
