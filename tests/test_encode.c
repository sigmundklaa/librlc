
#include <stdbool.h>
#include <string.h>
#include <unity/unity.h>

#include <rlc/buf.h>
#include <rlc/rlc.h>

#include "encode.h"

#include "harness.h"

#define buf_init(ctx_, buf_, data_)                                            \
        do {                                                                   \
                buf_ = alloc_buf(&ctx, 10);                                    \
                data_ = rlc_buf_ci_data(rlc_buf_ci_init(&buf_));               \
        } while (0)

#define buf_reset(ctx_, buf_, data_)                                           \
        do {                                                                   \
                rlc_buf_decref(buf_);                                          \
                buf_init(ctx_, buf_, data_);                                   \
        } while (0)

static struct rlc_context dummy_ctx;

void setUp(void)
{
        harness_setup(&dummy_ctx, RLC_AM);
}

void tearDown(void)
{
        harness_teardown(&dummy_ctx);
}

static rlc_buf alloc_buf(struct rlc_context *ctx, size_t size)
{
        rlc_buf ret = rlc_buf_alloc(ctx, size);
        TEST_ASSERT(rlc_buf_okay(ret));

        return ret;
}

static void test_encode_umd(void)
{
        rlc_errno status;
        struct rlc_config conf = {
                .sn_width = RLC_SN_6BIT,
        };
        rlc_context ctx = {
                .conf = &conf,
                .type = RLC_UM,
                .methods = dummy_ctx.methods,
        };
        struct rlc_pdu pdu = {
                .sn = 0xaaa,
                .seg_offset = 0x5555,
                .flags.polled = 0,
                .flags.is_last = 1,
                .flags.is_first = 1,
        };
        uint8_t expect[10];
        rlc_buf buf;
        uint8_t *data;

        buf_init(&ctx, buf, data);

        (void)memset(expect, 0, sizeof(expect));

        /* Test full SDU */
        rlc_pdu_encode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_size_t(1, rlc_buf_size(buf));

        expect[0] = 0b00 << 6;
        TEST_ASSERT_EQUAL_MEMORY(expect, data, 1);

        (void)memset(expect, 0, sizeof(expect));
        buf_reset(&ctx, buf, data);

        /* Test first of SDU */
        pdu.flags.is_last = 0;
        rlc_pdu_encode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_size_t(1, rlc_buf_size(buf));

        expect[0] = (0b01 << 6) | 0x2a;
        TEST_ASSERT_EQUAL_MEMORY(expect, data, 1);

        (void)memset(expect, 0, sizeof(expect));
        buf_reset(&ctx, buf, data);

        /* Test not first, not last */
        pdu.flags.is_first = 0;
        rlc_pdu_encode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_size_t(3, rlc_buf_size(buf));

        expect[0] = (0b11 << 6) | 0x2a;
        expect[1] = 0x55;
        expect[2] = 0x55;
        TEST_ASSERT_EQUAL_MEMORY(expect, data, 3);

        (void)memset(expect, 0, sizeof(expect));
        buf_reset(&ctx, buf, data);

        /* Test 12 bit SN */
        conf.sn_width = RLC_SN_12BIT;
        rlc_pdu_encode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_size_t(4, rlc_buf_size(buf));

        expect[0] = (0b11 << 6) | 0xa;
        expect[1] = 0xaa;
        expect[2] = 0x55;
        expect[3] = 0x55;
        TEST_ASSERT_EQUAL_MEMORY(expect, data, 4);

        rlc_buf_decref(buf);
}

static void test_decode_umd(void)
{
        rlc_errno status;
        struct rlc_config conf = {
                .sn_width = RLC_SN_6BIT,
        };
        rlc_context ctx = {
                .conf = &conf,
                .type = RLC_UM,
                .methods = dummy_ctx.methods,
        };
        struct rlc_pdu pdu;
        rlc_buf buf;
        uint8_t *data;

        /* Without SN and SO */
        buf_init(&ctx, buf, data);
        (void)rlc_buf_ci_reserve_tail(rlc_buf_ci_init(&buf),
                                      rlc_buf_tailroom(buf));

        data[0] = 0b00 << 6;
        status = rlc_pdu_decode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_first);

        /* With SN */
        buf_reset(&ctx, buf, data);
        (void)rlc_buf_ci_reserve_tail(rlc_buf_ci_init(&buf),
                                      rlc_buf_tailroom(buf));
        data[0] = (0b01 << 6) | 0x2a;
        status = rlc_pdu_decode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0x2a, pdu.sn);

        /* With SN and SO */
        buf_reset(&ctx, buf, data);
        (void)rlc_buf_ci_reserve_tail(rlc_buf_ci_init(&buf),
                                      rlc_buf_tailroom(buf));
        data[0] = (0b11 << 6) | 0x2a;
        data[1] = 0x55;
        data[2] = 0x55;
        status = rlc_pdu_decode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0x2a, pdu.sn);
        TEST_ASSERT_EQUAL_INT(0x5555, pdu.seg_offset);

        /* 12bit */
        buf_reset(&ctx, buf, data);
        (void)rlc_buf_ci_reserve_tail(rlc_buf_ci_init(&buf),
                                      rlc_buf_tailroom(buf));
        conf.sn_width = RLC_SN_12BIT;
        data[0] = (0b11 << 6) | 0xa;
        data[1] = 0xaa;
        data[2] = 0x55;
        data[3] = 0x55;
        status = rlc_pdu_decode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0xaaa, pdu.sn);
        TEST_ASSERT_EQUAL_INT(0x5555, pdu.seg_offset);

        rlc_buf_decref(buf);
}

static void test_encode_status(void)
{
        struct rlc_config conf = {
                .sn_width = RLC_SN_18BIT,
        };
        rlc_context ctx = {
                .conf = &conf,
                .type = RLC_AM,
                .methods = dummy_ctx.methods,
        };
        ssize_t ret;
        struct rlc_pdu_status status = {
                .ext.has_more = 0,
                .ext.has_range = 0,
                .ext.has_offset = 0,
                .offset.start = 0x1234,
                .offset.end = 0xabcd,
                .range = 0xea,
                .nack_sn = (0x5678 << 2) | 0x2,
        };
        struct rlc_pdu pdu = {
                .flags.is_status = 1,
                .sn = status.nack_sn,
        };
        rlc_buf buf;
        uint8_t *data;

        buf_init(&ctx, buf, data);

        rlc_pdu_encode(&ctx, &pdu, &buf);

        TEST_ASSERT_EQUAL(status.nack_sn >> 14, data[0]);
        TEST_ASSERT_EQUAL((status.nack_sn >> 6) & 0xff, data[1]);
        TEST_ASSERT_EQUAL((status.nack_sn & 0x3f) << 2, data[1]);

        buf_reset(&ctx, buf, data);

        status.ext.has_offset = 1;
        status.ext.has_range = 1;
        rlc_status_encode(&ctx, &status, &buf);

        TEST_ASSERT_EQUAL_UINT8(0x56, data[0]);
        TEST_ASSERT_EQUAL_UINT8(0x78, data[1]);
        TEST_ASSERT_EQUAL_UINT8((0x2 << 6) | (0b011 << 3), data[2]);
        TEST_ASSERT_EQUAL_UINT8(0x12, data[3]);
        TEST_ASSERT_EQUAL_UINT8(0x34, data[4]);
        TEST_ASSERT_EQUAL_UINT8(0xab, data[5]);
        TEST_ASSERT_EQUAL_UINT8(0xcd, data[6]);
        TEST_ASSERT_EQUAL_UINT8(0xea, data[7]);
        TEST_ASSERT_EQUAL_size_t(8, rlc_buf_size(buf));

        rlc_buf_decref(buf);
}

static void test_decode_status(void)
{
        struct rlc_config conf = {
                .sn_width = RLC_SN_18BIT,
        };
        rlc_context ctx = {
                .conf = &conf,
                .type = RLC_AM,
                .methods = dummy_ctx.methods,
        };
        ssize_t ret;
        struct rlc_pdu_status status;
        struct rlc_pdu pdu;
        rlc_buf buf;
        uint8_t *data;
        uint32_t sn;

        buf_init(&ctx, buf, data);
        (void)rlc_buf_ci_reserve_tail(rlc_buf_ci_init(&buf), 3);

        sn = 0x12343;
        data[0] = (sn >> 14) & 0xf;
        data[1] = (sn >> 6) & 0xff;
        data[2] = (sn & 0x3f) << 2;

        ret = rlc_pdu_decode(&ctx, &pdu, &buf);

        TEST_ASSERT_EQUAL_size_t(0, (size_t)ret);
        TEST_ASSERT_EQUAL_INT32(sn, pdu.sn);

        buf_reset(&ctx, buf, data);
        (void)rlc_buf_ci_reserve_tail(rlc_buf_ci_init(&buf),
                                      rlc_buf_tailroom(buf));

        (void)memset(&status, 0, sizeof(status));

        data[0] = 0x56;        /* NACK SN */
        data[1] = 0x78;        /* NACK SN */
        data[2] = 0x2 << 6;    /* NACK SN */
        data[2] |= 0b011 << 3; /* E1, E2 and E3 */

        data[3] = 0x12; /* SOstart */
        data[4] = 0x34; /* SOstart */
        data[5] = 0xab; /* SOend */
        data[6] = 0xcd; /* SOend */
        data[7] = 0xea; /* Range */

        ret = rlc_status_decode(&ctx, &status, &buf);

        TEST_ASSERT_EQUAL_size_t(0, (size_t)ret);
        TEST_ASSERT_EQUAL_INT(0x1234, status.offset.start);
        TEST_ASSERT_EQUAL_INT(0xabcd, status.offset.end);
        TEST_ASSERT_EQUAL_INT(0xea, status.range);
        TEST_ASSERT_EQUAL_INT((0x5678 << 2) | 0x2, status.nack_sn);
        TEST_ASSERT_EQUAL_INT(1, status.ext.has_offset);
        TEST_ASSERT_EQUAL_INT(1, status.ext.has_range);
        TEST_ASSERT_EQUAL_INT(0, status.ext.has_more);

        rlc_buf_decref(buf);
}

int main(void)
{
        UnityBegin(__FILE__);

        RUN_TEST(test_encode_umd);
        RUN_TEST(test_decode_umd);
        RUN_TEST(test_encode_status);
        RUN_TEST(test_decode_status);

        return UnityEnd();
}
