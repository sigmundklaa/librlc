
#include <rlc/rlc.h>
#include <rlc/sdu.h>
#include <rlc/list.h>

#include <string.h>

#include "common.h"
#include "log.h"

struct rlc_sdu *rlc_sdu_alloc(struct rlc_context *ctx, bool is_tx)
{
        struct rlc_sdu *sdu;

        sdu = rlc_alloc(ctx, sizeof(*sdu));
        if (sdu == NULL) {
                return NULL;
        }

        (void)memset(sdu, 0, sizeof(*sdu));

        sdu->refcount = 1;
        sdu->ctx = ctx;
        sdu->is_tx = is_tx;

        return sdu;
}

void rlc_sdu_incref(struct rlc_sdu *sdu)
{
        sdu->refcount++;
}

void rlc_sdu_decref(struct rlc_sdu *sdu)
{
        if (--sdu->refcount == 0) {
                if (sdu->is_tx) {
                        gabs_pbuf_decref(sdu->tx.buffer);
                        rlc_seg_list_clear(&sdu->tx.unsent,
                                           sdu->ctx->alloc_misc);
                } else {
                        rlc_seg_buf_destroy(&sdu->rx.buffer,
                                            sdu->ctx->alloc_misc);
                }

                rlc_dealloc(sdu->ctx, sdu);
        }
}

void rlc_sdu_queue_insert(rlc_sdu_queue *q, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        rlc_list_it it;

        rlc_list_foreach(q, it)
        {
                cur = rlc_sdu_from_it(it);

                if (sdu->sn <= cur->sn) {
                        break;
                }
        }

        (void)rlc_list_it_put_front(it, &sdu->list_node);
}

void rlc_sdu_queue_remove(rlc_sdu_queue *q, struct rlc_sdu *sdu)
{
        struct rlc_sdu *cur;
        rlc_list_it it;

        rlc_list_foreach(q, it)
        {
                cur = rlc_sdu_from_it(it);

                if (cur == sdu) {
                        (void)rlc_list_it_pop(it, NULL);
                        return;
                }
        }

        rlc_assert(0);
}

void rlc_sdu_queue_clear(rlc_sdu_queue *q)
{
        rlc_list_it it;
        struct rlc_sdu *sdu;

        rlc_list_foreach(q, it)
        {
                sdu = rlc_sdu_from_it(it);
                it = rlc_list_it_pop(it, NULL);

                rlc_sdu_decref(sdu);
        }
}

struct rlc_sdu *rlc_sdu_queue_get(rlc_sdu_queue *q, uint32_t sn)
{
        struct rlc_sdu *sdu;
        rlc_list_it it;

        rlc_list_foreach(q, it)
        {
                sdu = rlc_sdu_from_it(it);

                if (sdu->sn == sn) {
                        return sdu;
                }
        }

        return NULL;
}
