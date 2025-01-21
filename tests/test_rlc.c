
#include <unity/unity.h>
#include <stdlib.h>

#include "../src/rlc.c"

void setUp(void)
{
}

void tearDown(void)
{
}

static void *mem_alloc(struct rlc_context *ctx, size_t size)
{
        return malloc(size);
}

static void mem_dealloc(struct rlc_context *ctx, void *mem)
{
        free(mem);
}

static const struct rlc_methods methods = {
        .mem_alloc = mem_alloc,
        .mem_dealloc = mem_dealloc,
};

static void test_seg_append(void)
{
        rlc_errno status;
        rlc_context ctx = {
                .methods = &methods,
        };
        struct rlc_sdu sdu = {0};

        status = seg_append_(&ctx, &sdu,
                             (struct rlc_segment){
                                     .start = 0,
                                     .end = 128,
                             });
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_NOT_NULL(sdu.segments);
        TEST_ASSERT_NULL(sdu.segments->next);

        TEST_ASSERT_EQUAL(0, sdu.segments->seg.start);
        TEST_ASSERT_EQUAL(128, sdu.segments->seg.end);

        status = seg_append_(&ctx, &sdu,
                             (struct rlc_segment){.start = 150, .end = 190});
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_NOT_NULL(sdu.segments->next);

        TEST_ASSERT_EQUAL(150, sdu.segments->next->seg.start);
        TEST_ASSERT_EQUAL(190, sdu.segments->next->seg.end);

        status = seg_append_(&ctx, &sdu,
                             (struct rlc_segment){.start = 210, .end = 240});
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_NOT_NULL(sdu.segments->next->next);

        TEST_ASSERT_EQUAL(210, sdu.segments->next->next->seg.start);
        TEST_ASSERT_EQUAL(240, sdu.segments->next->next->seg.end);

        status = seg_append_(&ctx, &sdu,
                             (struct rlc_segment){.start = 200, .end = 205});
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_NOT_NULL(sdu.segments->next->next);
        TEST_ASSERT_NOT_NULL(sdu.segments->next->next->next);

        TEST_ASSERT_EQUAL(200, sdu.segments->next->next->seg.start);
        TEST_ASSERT_EQUAL(205, sdu.segments->next->next->seg.end);

        status = seg_append_(&ctx, &sdu,
                             (struct rlc_segment){.start = 190, .end = 195});
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_NOT_NULL(sdu.segments->next);

        TEST_ASSERT_EQUAL(150, sdu.segments->next->seg.start);
        TEST_ASSERT_EQUAL(195, sdu.segments->next->seg.end);
}

int main(void)
{
        UnityBegin(__FILE__);

        RUN_TEST(test_seg_append);

        return UnityEnd();
}
