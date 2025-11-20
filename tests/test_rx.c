
#include <string.h>
#include <errno.h>

#include <unity/unity.h>

#include <rlc/rlc.h>
#include <rlc/buf.h>
#include <rlc/sdu.h>

#include "harness.h"
#include "rx.c"

static struct rlc_context ctx;

void setUp(void)
{
        harness_setup(&ctx, RLC_AM);
}

void tearDown(void)
{
        harness_teardown(&ctx);
}

static void test_insert_buf(void)
{
        struct rlc_segment seg;
        struct rlc_segment uniq;
        struct rlc_sdu *sdu;
        rlc_buf buf;
        rlc_errno status;
        uint8_t check[20];
        size_t size;

        sdu = rlc_sdu_alloc(&ctx, RLC_RX);

        seg.start = 0;
        seg.end = 3;
        buf = rlc_buf_alloc(&ctx, seg.end - seg.start);
        TEST_ASSERT(rlc_buf_okay(buf));
        rlc_buf_put(&buf, (void *)"111", 3);
        status = insert_buffer(&ctx, sdu, &buf, seg);
        TEST_ASSERT_EQUAL(0, status);
        size = rlc_buf_copy(sdu->buffer, check, 0, sizeof(check));
        TEST_ASSERT_EQUAL(3, size);
        TEST_ASSERT_EQUAL_MEMORY("111", check, 3);
        rlc_buf_decref(buf);

        seg.start = 5;
        seg.end = 9;
        buf = rlc_buf_alloc(&ctx, seg.end - seg.start);
        TEST_ASSERT(rlc_buf_okay(buf));
        rlc_buf_put(&buf, (void *)"222", 4);
        status = insert_buffer(&ctx, sdu, &buf, seg);
        TEST_ASSERT_EQUAL(0, status);
        size = rlc_buf_copy(sdu->buffer, check, 0, sizeof(check));
        TEST_ASSERT_EQUAL(7, size);
        TEST_ASSERT_EQUAL_MEMORY("111222", check, 7);
        rlc_buf_decref(buf);

        seg.start = 4;
        seg.end = 5;
        buf = rlc_buf_alloc(&ctx, seg.end - seg.start);
        TEST_ASSERT(rlc_buf_okay(buf));
        rlc_buf_put(&buf, (void *)"3", 1);
        status = insert_buffer(&ctx, sdu, &buf, seg);
        TEST_ASSERT_EQUAL(0, status);
        size = rlc_buf_copy(sdu->buffer, check, 0, sizeof(check));
        TEST_ASSERT_EQUAL(8, size);
        TEST_ASSERT_EQUAL_MEMORY("1113222", check, 8);
        rlc_buf_decref(buf);

        seg.start = 3;
        seg.end = 4;
        buf = rlc_buf_alloc(&ctx, seg.end - seg.start);
        TEST_ASSERT(rlc_buf_okay(buf));
        rlc_buf_put(&buf, (void *)"4", 1);
        status = insert_buffer(&ctx, sdu, &buf, seg);
        TEST_ASSERT_EQUAL(0, status);
        size = rlc_buf_copy(sdu->buffer, check, 0, sizeof(check));
        TEST_ASSERT_EQUAL(9, size);
        TEST_ASSERT_EQUAL_MEMORY("11143222", check, 9);
        rlc_buf_decref(buf);

        rlc_sdu_decref(&ctx, sdu);
}

int main(void)
{
        UnityBegin(__FILE__);

        RUN_TEST(test_insert_buf);

        return UnityEnd();
}
