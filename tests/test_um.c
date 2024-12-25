
#include <stdbool.h>
#include <unity/unity.h>

#include <rlc/rlc.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_chunks_size(void)
{
        TEST_ASSERT_TRUE(false);
}

int main(void)
{
        UnityBegin(__FILE__);

        RUN_TEST(test_chunks_size);

        return UnityEnd();
}
