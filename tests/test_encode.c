
#include <stdbool.h>
#include <string.h>
#include <unity/unity.h>

#include <rlc/chunks.h>
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
        rlc_context ctx = {
                .sn_width = RLC_SN_6BIT,
                .type = RLC_UM,
        };
        struct rlc_pdu pdu = {
                .sn = 0xaaa,
                .seg_offset = 0x5555,
                .flags.polled = 0,
                .flags.is_last = 1,
                .flags.is_first = 1,
        };
        uint8_t buf[10];
        uint8_t expect[10];
        struct rlc_chunk chunk = {
                .data = buf,
        };

        (void)memset(buf, 0, sizeof(buf));
        (void)memset(expect, 0, sizeof(expect));

        /* Test full SDU */
        rlc_pdu_encode(&ctx, &pdu, &chunk);
        TEST_ASSERT_EQUAL_size_t(1, chunk.size);

        expect[0] = 0b00 << 6;
        TEST_ASSERT_EQUAL_MEMORY(expect, buf, 1);

        (void)memset(buf, 0, sizeof(buf));
        (void)memset(expect, 0, sizeof(expect));

        /* Test first of SDU */
        pdu.flags.is_last = 0;
        rlc_pdu_encode(&ctx, &pdu, &chunk);
        TEST_ASSERT_EQUAL_size_t(1, chunk.size);

        expect[0] = (0b01 << 6) | 0x2a;
        TEST_ASSERT_EQUAL_MEMORY(expect, buf, 1);

        (void)memset(buf, 0, sizeof(buf));
        (void)memset(expect, 0, sizeof(expect));

        /* Test not first, not last */
        pdu.flags.is_first = 0;
        rlc_pdu_encode(&ctx, &pdu, &chunk);
        TEST_ASSERT_EQUAL_size_t(3, chunk.size);

        expect[0] = (0b11 << 6) | 0x2a;
        expect[1] = 0x55;
        expect[2] = 0x55;
        TEST_ASSERT_EQUAL_MEMORY(expect, buf, 3);

        (void)memset(buf, 0, sizeof(buf));
        (void)memset(expect, 0, sizeof(expect));

        /* Test 12 bit SN */
        ctx.sn_width = RLC_SN_12BIT;
        rlc_pdu_encode(&ctx, &pdu, &chunk);
        TEST_ASSERT_EQUAL_size_t(4, chunk.size);

        expect[0] = (0b11 << 6) | 0xa;
        expect[1] = 0xaa;
        expect[2] = 0x55;
        expect[3] = 0x55;
        TEST_ASSERT_EQUAL_MEMORY(expect, buf, 4);
}

static void test_decode_umd(void)
{
        rlc_errno status;
        uint8_t buf[4];
        rlc_context ctx = {
                .type = RLC_UM,
                .sn_width = RLC_SN_6BIT,
        };
        struct rlc_pdu pdu;
        struct rlc_chunk chunks[] = {
                {.data = buf, .size = 1},
                {.data = buf + 1, .size = 2},
                {.data = buf + 3, .size = 1},
        };
        chunks[0].next = &chunks[1];
        chunks[1].next = &chunks[2];

        /* Without SN and SO */
        buf[0] = 0b00 << 6;
        status = rlc_pdu_decode(&ctx, &pdu, chunks);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_first);

        /* With SN */
        buf[0] = (0b01 << 6) | 0x2a;
        status = rlc_pdu_decode(&ctx, &pdu, chunks);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0x2a, pdu.sn);

        /* With SN and SO */
        buf[0] = (0b11 << 6) | 0x2a;
        buf[1] = 0x55;
        buf[2] = 0x55;
        status = rlc_pdu_decode(&ctx, &pdu, chunks);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0x2a, pdu.sn);
        TEST_ASSERT_EQUAL_INT(0x5555, pdu.seg_offset);

        /* 12bit */
        ctx.sn_width = RLC_SN_12BIT;
        buf[0] = (0b11 << 6) | 0xa;
        buf[1] = 0xaa;
        buf[2] = 0x55;
        buf[3] = 0x55;
        status = rlc_pdu_decode(&ctx, &pdu, chunks);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0xaaa, pdu.sn);
        TEST_ASSERT_EQUAL_INT(0x5555, pdu.seg_offset);
}

static void test_encode_status(void)
{
        rlc_context ctx = {
                .type = RLC_AM,
                .sn_width = RLC_SN_18BIT,
        };
        uint8_t buf[8] = {0};
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
        struct rlc_chunk chunk = {
                .data = buf,
        };

        rlc_status_encode(&ctx, &status, &chunk);

        TEST_ASSERT_EQUAL_UINT8(0x56, buf[0]);
        TEST_ASSERT_EQUAL_UINT8(0x78, buf[1]);
        TEST_ASSERT_EQUAL_UINT8((0x2 << 6) | (0b011 << 3), buf[2]);
        TEST_ASSERT_EQUAL_UINT8(0x12, buf[3]);
        TEST_ASSERT_EQUAL_UINT8(0x34, buf[4]);
        TEST_ASSERT_EQUAL_UINT8(0xab, buf[5]);
        TEST_ASSERT_EQUAL_UINT8(0xcd, buf[6]);
        TEST_ASSERT_EQUAL_UINT8(0xea, buf[7]);
        TEST_ASSERT_EQUAL_size_t(8, chunk.size);
}

static void test_decode_status(void)
{
        rlc_context ctx = {
                .type = RLC_AM,
                .sn_width = RLC_SN_18BIT,
        };
        uint8_t buf[8];
        ssize_t ret;
        struct rlc_pdu_status status;
        struct rlc_chunk chunk = {
                .data = buf,
                .size = 8,
        };

        buf[0] = 0x56;        /* NACK SN */
        buf[1] = 0x78;        /* NACK SN */
        buf[2] = 0x2 << 6;    /* NACK SN */
        buf[2] |= 0b011 << 3; /* E1, E2 and E3 */

        buf[3] = 0x12; /* SOstart */
        buf[4] = 0x34; /* SOstart */
        buf[5] = 0xab; /* SOend */
        buf[6] = 0xcd; /* SOend */
        buf[7] = 0xea; /* Range */

        ret = rlc_status_decode(&ctx, &status, &chunk, 0);

        TEST_ASSERT_EQUAL_size_t(8, (size_t)ret);
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
