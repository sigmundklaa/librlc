
#include <stdbool.h>
#include <string.h>
#include <unity/unity.h>

#include <rlc/chunks.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static bool chunk_cmp_(const struct rlc_chunk *c1, const struct rlc_chunk *c2)
{
        const struct rlc_chunk *cur1;
        const struct rlc_chunk *cur2;

        cur2 = c2;

        for (rlc_each_node(c1, cur1, next)) {
                if (cur2 == NULL || cur2->size != cur1->size) {
                        return false;
                }

                if (memcmp(cur2->data, cur1->data, cur2->size) != 0) {
                        return false;
                }

                cur2 = cur2->next;
        }

        return true;
}

static void test_chunks_size(void)
{
        struct rlc_chunk chunks[] = {
                {.size = 4},
                {.size = 5},
                {.size = 3},
        };

        chunks[0].next = &chunks[1];
        chunks[1].next = &chunks[2];

        TEST_ASSERT_EQUAL_size_t(12, rlc_chunks_size(chunks));
}

static void test_num_chunks(void)
{
        struct rlc_chunk chunks[] = {
                {.size = 4}, {.size = 5}, {.size = 3},
                {.size = 4}, {.size = 5}, {.size = 3},
        };

        chunks[0].next = &chunks[1];
        chunks[1].next = &chunks[2];
        chunks[2].next = &chunks[3];
        chunks[3].next = &chunks[4];
        chunks[4].next = &chunks[5];
        chunks[5].next = &chunks[6];

        TEST_ASSERT_EQUAL_size_t(5, rlc_chunks_num(chunks, 18));
        TEST_ASSERT_EQUAL_size_t(5, rlc_chunks_num(chunks, 17));
        TEST_ASSERT_EQUAL_size_t(4, rlc_chunks_num(chunks, 16));

        TEST_ASSERT_EQUAL_size_t(4, rlc_chunks_num_view(chunks, 13, 2));
        TEST_ASSERT_EQUAL_size_t(3, rlc_chunks_num_view(chunks, 12, 4));
        TEST_ASSERT_EQUAL_size_t(4, rlc_chunks_num_view(chunks, 13, 4));
}

static void test_chunks_deepcopy(void)
{
        struct rlc_chunk chunks[] = {
                {.data = "Test1", .size = 6},
                {.data = "T2", .size = 3},
                {.data = "TS3", .size = 4},
        };
        char buf[20];

        chunks[0].next = &chunks[1];
        chunks[1].next = &chunks[2];

        (void)memset(buf, 0, sizeof(buf));

        TEST_ASSERT_EQUAL_INT(13, rlc_chunks_deepcopy(chunks, buf, 20));
        TEST_ASSERT_EQUAL_MEMORY("Test1\0T2\0TS3\0", buf, 13);

        (void)memset(buf, 0, sizeof(buf));

        TEST_ASSERT_EQUAL_INT(7, rlc_chunks_deepcopy(chunks, buf, 7));
        TEST_ASSERT_EQUAL_MEMORY("Test1\0T\0", buf, 7);

        (void)memset(buf, 0, sizeof(buf));

        TEST_ASSERT_EQUAL_INT(7, rlc_chunks_deepcopy_view(chunks, buf, 7, 6));
        TEST_ASSERT_EQUAL_MEMORY("T2\0TS3\0", buf, 7);

        (void)memset(buf, 0, sizeof(buf));

        TEST_ASSERT_EQUAL_INT(4, rlc_chunks_deepcopy_view(chunks, buf, 4, 6));
        TEST_ASSERT_EQUAL_MEMORY("T2\0T", buf, 4);

        (void)memset(buf, 0, sizeof(buf));

        TEST_ASSERT_EQUAL_INT(3, rlc_chunks_deepcopy_view(chunks, buf, 3, 6));
        TEST_ASSERT_EQUAL_MEMORY("T2\0", buf, 3);
}

static void test_chunks_copy(void)
{
        struct rlc_chunk chunks[] = {
                {.data = "Test1", .size = 6},
                {.data = "T2", .size = 3},
                {.data = "TS3", .size = 4},
        };
        struct rlc_chunk chunks_copied[3] = {0};

        chunks[0].next = &chunks[1];
        chunks[1].next = &chunks[2];

        TEST_ASSERT_EQUAL_INT(13, rlc_chunks_copy(chunks, chunks_copied, 13));
        TEST_ASSERT_TRUE(chunk_cmp_(chunks, chunks_copied));

        (void)memset(chunks_copied, 0, sizeof(chunks_copied));

        TEST_ASSERT_EQUAL_INT(7, rlc_chunks_copy(chunks, chunks_copied, 7));
        TEST_ASSERT_EQUAL_MEMORY("Test1\0", chunks_copied[0].data, 6);
        TEST_ASSERT_EQUAL_MEMORY("T", chunks_copied[1].data, 1);
        TEST_ASSERT_EQUAL_size_t(6, chunks_copied[0].size);
        TEST_ASSERT_EQUAL_size_t(1, chunks_copied[1].size);

        (void)memset(chunks_copied, 0, sizeof(chunks_copied));

        TEST_ASSERT_EQUAL_INT(6, rlc_chunks_copy(chunks, chunks_copied, 6));
        TEST_ASSERT_EQUAL_MEMORY("Test1\0", chunks_copied[0].data, 6);
        TEST_ASSERT_EQUAL_size_t(6, chunks_copied[0].size);

        (void)memset(chunks_copied, 0, sizeof(chunks_copied));

        TEST_ASSERT_EQUAL_INT(
                5, rlc_chunks_copy_view(chunks, chunks_copied, 5, 4));
        TEST_ASSERT_EQUAL_MEMORY("1\0", chunks_copied[0].data, 2);
        TEST_ASSERT_EQUAL_MEMORY("T2\0", chunks_copied[1].data, 3);
        TEST_ASSERT_EQUAL_size_t(2, chunks_copied[0].size);
        TEST_ASSERT_EQUAL_size_t(3, chunks_copied[1].size);

        (void)memset(chunks_copied, 0, sizeof(chunks_copied));

        TEST_ASSERT_EQUAL_INT(
                3, rlc_chunks_copy_view(chunks, chunks_copied, 3, 4));
        TEST_ASSERT_EQUAL_MEMORY("1\0", chunks_copied[0].data, 2);
        TEST_ASSERT_EQUAL_MEMORY("T", chunks_copied[1].data, 1);
        TEST_ASSERT_EQUAL_size_t(2, chunks_copied[0].size);
        TEST_ASSERT_EQUAL_size_t(1, chunks_copied[1].size);
}

int main(void)
{
        UnityBegin(__FILE__);

        RUN_TEST(test_chunks_size);
        RUN_TEST(test_num_chunks);
        RUN_TEST(test_chunks_deepcopy);
        RUN_TEST(test_chunks_copy);

        return UnityEnd();
}
