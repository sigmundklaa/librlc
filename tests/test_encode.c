
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
                .type = RLC_UM,
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
        size_t num_chunks = sizeof(chunks) / sizeof(chunks[0]);

        /* Without SN and SO */
        buf[0] = 0b00 << 6;
        status = rlc_pdu_decode(&ctx, &pdu, chunks, num_chunks);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_first);

        /* With SN */
        buf[0] = (0b01 << 6) | 0x2a;
        status = rlc_pdu_decode(&ctx, &pdu, chunks, num_chunks);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(1, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0x2a, pdu.sn);

        /* With SN and SO */
        buf[0] = (0b11 << 6) | 0x2a;
        buf[1] = 0x55;
        buf[2] = 0x55;
        status = rlc_pdu_decode(&ctx, &pdu, chunks, num_chunks);
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
        status = rlc_pdu_decode(&ctx, &pdu, chunks, num_chunks);
        TEST_ASSERT_EQUAL_INT(0, status);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_last);
        TEST_ASSERT_EQUAL_INT(0, pdu.flags.is_first);
        TEST_ASSERT_EQUAL_INT(0xaaa, pdu.sn);
        TEST_ASSERT_EQUAL_INT(0x5555, pdu.seg_offset);
}

int main(void)
{
        UnityBegin(__FILE__);

        RUN_TEST(test_encode_umd);
        RUN_TEST(test_decode_umd);

        return UnityEnd();
}
