
#include <stdbool.h>
#include <string.h>
#include <unity/unity.h>

#include <rlc/buf.h>
#include <rlc/rlc.h>

#include "encode.h"

void setUp(void)
{
}

void tearDown(void)
{
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
        };
        struct rlc_pdu pdu = {
                .sn = 0xaaa,
                .seg_offset = 0x5555,
                .flags.polled = 0,
                .flags.is_last = 1,
                .flags.is_first = 1,
        };
        uint8_t data[10];
        uint8_t expect[10];
        struct rlc_buf buf = {
                .data = data,
                .cap = sizeof(data),
        };

        (void)memset(data, 0, sizeof(data));
        (void)memset(expect, 0, sizeof(expect));

        /* Test full SDU */
        rlc_pdu_encode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_size_t(1, buf.size);

        expect[0] = 0b00 << 6;
        TEST_ASSERT_EQUAL_MEMORY(expect, buf.data, 1);

        (void)memset(data, 0, sizeof(data));
        (void)memset(expect, 0, sizeof(expect));
        buf.size = 0;

        /* Test first of SDU */
        pdu.flags.is_last = 0;
        rlc_pdu_encode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_size_t(1, buf.size);

        expect[0] = (0b01 << 6) | 0x2a;
        TEST_ASSERT_EQUAL_MEMORY(expect, buf.data, 1);

        (void)memset(data, 0, sizeof(data));
        (void)memset(expect, 0, sizeof(expect));
        buf.size = 0;

        /* Test not first, not last */
        pdu.flags.is_first = 0;
        rlc_pdu_encode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_size_t(3, buf.size);

        expect[0] = (0b11 << 6) | 0x2a;
        expect[1] = 0x55;
        expect[2] = 0x55;
        TEST_ASSERT_EQUAL_MEMORY(expect, buf.data, 3);

        (void)memset(data, 0, sizeof(data));
        (void)memset(expect, 0, sizeof(expect));
        buf.size = 0;

        /* Test 12 bit SN */
        conf.sn_width = RLC_SN_12BIT;
        rlc_pdu_encode(&ctx, &pdu, &buf);
        TEST_ASSERT_EQUAL_size_t(4, buf.size);

        expect[0] = (0b11 << 6) | 0xa;
        expect[1] = 0xaa;
        expect[2] = 0x55;
        expect[3] = 0x55;
        TEST_ASSERT_EQUAL_MEMORY(expect, buf.data, 4);
}

static void test_decode_umd(void)
{
        rlc_errno status;
        uint8_t data[4];
        struct rlc_config conf = {
                .sn_width = RLC_SN_6BIT,
        };
        rlc_context ctx = {
                .conf = &conf,
                .type = RLC_UM,
        };
        struct rlc_pdu pdu;
        struct rlc_buf buf = {
                .data = data,
                .cap = sizeof(data),
                .size = sizeof(data),
        };
        struct rlc_buf *ptr = &buf;

        /* Without SN and SO */
        data[0] = 0b00 << 6;
        status = rlc_pdu_decode(&ctx, &pdu, &ptr);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_first);

        /* With SN */
        buf.data = data;
        buf.size = sizeof(data);
        data[0] = (0b01 << 6) | 0x2a;
        status = rlc_pdu_decode(&ctx, &pdu, &ptr);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0x2a, pdu.sn);

        /* With SN and SO */
        buf.data = data;
        buf.size = sizeof(data);
        data[0] = (0b11 << 6) | 0x2a;
        data[1] = 0x55;
        data[2] = 0x55;
        status = rlc_pdu_decode(&ctx, &pdu, &ptr);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0x2a, pdu.sn);
        TEST_ASSERT_EQUAL_INT(0x5555, pdu.seg_offset);

        /* 12bit */
        buf.data = data;
        buf.size = sizeof(data);
        conf.sn_width = RLC_SN_12BIT;
        data[0] = (0b11 << 6) | 0xa;
        data[1] = 0xaa;
        data[2] = 0x55;
        data[3] = 0x55;
        status = rlc_pdu_decode(&ctx, &pdu, &ptr);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0xaaa, pdu.sn);
        TEST_ASSERT_EQUAL_INT(0x5555, pdu.seg_offset);
}

static void test_encode_status(void)
{
        struct rlc_config conf = {
                .sn_width = RLC_SN_18BIT,
        };
        rlc_context ctx = {
                .conf = &conf,
                .type = RLC_AM,
        };
        uint8_t data[8] = {0};
        ssize_t ret;
        struct rlc_pdu_status status = {
                .ext.has_more = 0,
                .ext.has_range = 1,
                .ext.has_offset = 1,
                .offset.start = 0x1234,
                .offset.end = 0xabcd,
                .range = 0xea,
                .nack_sn = (0x5678 << 2) | 0x2,
        };
        struct rlc_buf buf = {
                .data = data,
                .cap = sizeof(data),
        };

        rlc_status_encode(&ctx, &status, &buf);

        TEST_ASSERT_EQUAL_UINT8(0x56, buf.data[0]);
        TEST_ASSERT_EQUAL_UINT8(0x78, buf.data[1]);
        TEST_ASSERT_EQUAL_UINT8((0x2 << 6) | (0b011 << 3), buf.data[2]);
        TEST_ASSERT_EQUAL_UINT8(0x12, buf.data[3]);
        TEST_ASSERT_EQUAL_UINT8(0x34, buf.data[4]);
        TEST_ASSERT_EQUAL_UINT8(0xab, buf.data[5]);
        TEST_ASSERT_EQUAL_UINT8(0xcd, buf.data[6]);
        TEST_ASSERT_EQUAL_UINT8(0xea, buf.data[7]);
        TEST_ASSERT_EQUAL_size_t(8, buf.size);
}

static void test_decode_status(void)
{
        struct rlc_config conf = {
                .sn_width = RLC_SN_18BIT,
        };
        rlc_context ctx = {
                .conf = &conf,
                .type = RLC_AM,
        };
        uint8_t data[8];
        ssize_t ret;
        struct rlc_pdu_status status;
        struct rlc_buf buf = {
                .data = data,
                .size = sizeof(data),
                .cap = sizeof(data),
        };
        struct rlc_buf *ptr = &buf;

        buf.data[0] = 0x56;        /* NACK SN */
        buf.data[1] = 0x78;        /* NACK SN */
        buf.data[2] = 0x2 << 6;    /* NACK SN */
        buf.data[2] |= 0b011 << 3; /* E1, E2 and E3 */

        buf.data[3] = 0x12; /* SOstart */
        buf.data[4] = 0x34; /* SOstart */
        buf.data[5] = 0xab; /* SOend */
        buf.data[6] = 0xcd; /* SOend */
        buf.data[7] = 0xea; /* Range */

        ret = rlc_status_decode(&ctx, &status, &ptr);

        TEST_ASSERT_EQUAL_size_t(0, (size_t)ret);
        TEST_ASSERT_EQUAL_INT(0x1234, status.offset.start);
        TEST_ASSERT_EQUAL_INT(0xabcd, status.offset.end);
        TEST_ASSERT_EQUAL_INT(0xea, status.range);
        TEST_ASSERT_EQUAL_INT((0x5678 << 2) | 0x2, status.nack_sn);
        TEST_ASSERT_EQUAL_INT(1, status.ext.has_offset);
        TEST_ASSERT_EQUAL_INT(1, status.ext.has_range);
        TEST_ASSERT_EQUAL_INT(0, status.ext.has_more);
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
