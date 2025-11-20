
#include <semaphore.h>
#include <pthread.h>
#include <errno.h>

#include <unity/unity.h>

#include <rlc/rlc.h>
#include <rlc/buf.h>

#include "harness.h"

#define TEST_DATA                                                              \
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "            \
        "Pellentesque aliquam ultricies vestibulum. Suspendisse potenti. "     \
        "Phasellus orci eros, varius ac purus ut, euismod facilisis felis. "   \
        "Pellentesque sed tempor tellus. Donec vel ante id metus fringilla "   \
        "pharetra vitae ut odio. Cras enim orci, auctor id feugiat vitae, "    \
        "facilisis sed libero. Nunc ac elementum diam. Donec sagittis dolor "  \
        "nec tristique euismod. Aenean a accumsan ligula, non lobortis mi. "   \
        "In hac habitasse platea dictumst. Quisque gravida tellus leo. "

struct context {
        rlc_context rlc;
        unsigned int drop_rate;
        size_t mtu;

        unsigned int drop_cnt;

        unsigned int rx_cnt;
        unsigned int rx_fail_cnt;
        unsigned int tx_cnt;
        unsigned int tx_fail_cnt;

        struct context *other;
};

void setUp(void)
{
}

void tearDown(void)
{
}

static struct context *get_ctx(struct rlc_context *rlc)
{
        return (struct context *)(((uint8_t *)rlc) -
                                  offsetof(struct context, rlc));
}

static void do_tx_avail(rlc_timer timer, struct rlc_context *rlc)
{
        struct context *ctx = get_ctx(rlc);

        (void)timer;
        rlc_tx_avail(rlc, ctx->mtu);
}

static void offloaded_tx_avail(struct context *ctx)
{
        rlc_timer timer;
        rlc_errno status;

        timer = rlc_timer_install(do_tx_avail, &ctx->rlc,
                                  RLC_TIMER_SINGLE | RLC_TIMER_UNLOCKED_CB);
        if (!rlc_timer_okay(timer)) {
                rlc_panicf(EBUSY, "Failed to alloc timer");
                return;
        }

        status = rlc_timer_start(timer, 1);
        if (status != 0) {
                rlc_panicf(status, "Unable to start timer");
        }
}

static rlc_errno tx_submit(struct rlc_context *rlc, rlc_buf buf)
{
        struct context *container = get_ctx(rlc);

        if (random() % 100 < container->drop_rate) {
                container->drop_cnt++;
                rlc_buf_decref(buf);

                return 0;
        }

        rlc_rx_submit(&container->other->rlc, buf);
        container->rx_cnt++;

        offloaded_tx_avail(container);

        return 0;
}

static rlc_errno tx_request(struct rlc_context *ctx)
{
        rlc_tx_avail(ctx, get_ctx(ctx)->mtu);
        return 0;
}

static void event(struct rlc_context *rlc, const struct rlc_event *event)
{
        struct context *ctx;

        ctx = get_ctx(rlc);

        switch (event->type) {
        case RLC_EVENT_RX_DONE:
                ctx->rx_cnt++;
                break;
        case RLC_EVENT_RX_FAIL:
                ctx->rx_fail_cnt++;
                break;
        case RLC_EVENT_TX_DONE:
                ctx->tx_cnt++;
                break;
        case RLC_EVENT_TX_FAIL:
                ctx->tx_fail_cnt++;
                break;
        default:
                assert(0);
        }
}

static const struct rlc_methods methods = {
        .tx_submit = tx_submit,
        .tx_request = tx_request,
        .event = event,
        .mem_alloc = harness_alloc,
        .mem_dealloc = harness_dealloc,
};

static const struct rlc_config conf = {
        .window_size = 1,
        .buffer_size = 1500,
        .byte_without_poll_max = 100,
        .pdu_without_poll_max = 5,
        .time_poll_retransmit_us = 500,
        .time_reassembly_us = 5000,
        .max_retx_threshhold = 5,
        .sn_width = RLC_SN_18BIT,
};

static struct context *ctx_create(unsigned int drop_rate, size_t mtu)
{
        struct context *ctx;
        rlc_errno status;

        ctx = calloc(1, sizeof(*ctx));
        TEST_ASSERT_NOT_EQUAL(NULL, ctx);

        ctx->drop_rate = drop_rate;
        ctx->mtu = mtu;

        status = rlc_init(&ctx->rlc, RLC_AM, &conf, &methods, NULL);
        TEST_ASSERT_EQUAL(0, status);

        return ctx;
}

static void ctx_destroy(struct context *ctx)
{
        rlc_errno status;

        status = rlc_deinit(&ctx->rlc);
        TEST_ASSERT_EQUAL(0, status);

        free(ctx);
}

static void test_arq(void)
{
        struct context *c1;
        struct context *c2;
        struct rlc_sdu *sdu;
        rlc_buf buf;
        size_t mtu = 50;
        size_t drop_rate = 30;
        int status;

        c1 = ctx_create(drop_rate, mtu);
        c2 = ctx_create(drop_rate, mtu);
        TEST_ASSERT_NOT_EQUAL(NULL, c1);
        TEST_ASSERT_NOT_EQUAL(NULL, c2);

        c1->other = c2;
        c2->other = c1;

        buf = rlc_buf_alloc(&c1->rlc, sizeof(TEST_DATA));
        TEST_ASSERT_TRUE(rlc_buf_okay(buf));

        rlc_buf_put(&buf, (uint8_t *)TEST_DATA, sizeof(TEST_DATA));

        status = rlc_send(&c1->rlc, buf, &sdu);
        TEST_ASSERT_EQUAL(0, status);

        status = rlc_sem_down(&sdu->tx_sem, -1);
        TEST_ASSERT_EQUAL(0, status);

        TEST_ASSERT_EQUAL(0, sdu->tx_status);

        ctx_destroy(c2);
        ctx_destroy(c1);
}

int main(void)
{
        UnityBegin(__FILE__);

        rlc_plat_init();

        RUN_TEST(test_arq);

        return UnityEnd();
}
