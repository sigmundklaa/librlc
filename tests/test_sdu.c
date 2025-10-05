
#include <string.h>
#include <errno.h>

#include <unity/unity.h>

#include <rlc/rlc.h>
#include <rlc/sdu.h>

#include "harness.h"

void setUp(void)
{
        harness_setup();
}

void tearDown(void)
{
        harness_teardown();
}

static void test_insert_segment_back(void)
{
        struct rlc_segment seg;
        struct rlc_segment uniq;
        struct rlc_sdu *sdu;
        rlc_errno status;

        sdu = rlc_sdu_alloc(&ctx, RLC_RX);

        /* Insert 0->3. Expected state after insert: (0->3) */
        seg.start = 0;
        seg.end = 3;

        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(0, uniq.start);
        TEST_ASSERT_EQUAL(3, uniq.end);
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(3, sdu->segments->seg.end);

        /* Insert 6->8. Expected state after insert: (0->3), (6->8) */
        seg.start = 6;
        seg.end = 8;

        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(6, uniq.start);
        TEST_ASSERT_EQUAL(8, uniq.end);
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(3, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL(6, sdu->segments->next->seg.start);
        TEST_ASSERT_EQUAL(8, sdu->segments->next->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next->next);

        /* Insert 5->7. Expected state after insert: (0->3), (5->8) */
        seg.start = 5;
        seg.end = 7;

        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(5, uniq.start);
        TEST_ASSERT_EQUAL(6, uniq.end);
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(3, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL(5, sdu->segments->next->seg.start);
        TEST_ASSERT_EQUAL(8, sdu->segments->next->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next->next);

        /* Insert 3->5. Expected state after insert: (0->8) */
        seg.start = 3;
        seg.end = 5;
        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(3, uniq.start);
        TEST_ASSERT_EQUAL(5, uniq.end);
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(8, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next);

        /* Insert 2->5. Expected state after insert:
         * (0->8) (should not modify) */
        seg.start = 2;
        seg.end = 5;
        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(-ENODATA, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&uniq));
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(8, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next);

        /* Insert 12->15. Expected state after insert: (0->8), (12->15) */
        seg.start = 12;
        seg.end = 15;
        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(12, uniq.start);
        TEST_ASSERT_EQUAL(15, uniq.end);
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(8, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL(12, sdu->segments->next->seg.start);
        TEST_ASSERT_EQUAL(15, sdu->segments->next->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next->next);

        /* Insert 6->10. Expected state after insert: (0->10), (12->15) */
        seg.start = 6;
        seg.end = 10;
        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(8, uniq.start);
        TEST_ASSERT_EQUAL(10, uniq.end);
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(10, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL(12, sdu->segments->next->seg.start);
        TEST_ASSERT_EQUAL(15, sdu->segments->next->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next->next);

        /* Insert 11->17. Expected state after insert: (0->10), (11->15) (will
         * require another call to insert 15->17) */
        seg.start = 11;
        seg.end = 17;
        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_TRUE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(15, seg.start);
        TEST_ASSERT_EQUAL(17, seg.end);
        TEST_ASSERT_EQUAL(11, uniq.start);
        TEST_ASSERT_EQUAL(12, uniq.end);
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(10, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL(11, sdu->segments->next->seg.start);
        TEST_ASSERT_EQUAL(15, sdu->segments->next->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next->next);

        /* Insert 15->17. Expected state after insert: (0->10), (11->17) */
        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(15, uniq.start);
        TEST_ASSERT_EQUAL(17, uniq.end);
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(10, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL(11, sdu->segments->next->seg.start);
        TEST_ASSERT_EQUAL(17, sdu->segments->next->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next->next);

        rlc_sdu_decref(&ctx, sdu);
}

static void test_insert_segment_front(void)
{
        struct rlc_segment seg;
        struct rlc_segment uniq;
        struct rlc_sdu *sdu;
        rlc_errno status;

        sdu = rlc_sdu_alloc(&ctx, RLC_RX);

        /* Insert 5->10. Expected state after insert: (5->10) */
        seg.start = 5;
        seg.end = 10;

        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(5, uniq.start);
        TEST_ASSERT_EQUAL(10, uniq.end);
        TEST_ASSERT_EQUAL(5, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(10, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next);

        /* Insert 3->10. Expected state after insert: (3->10) */
        seg.start = 3;
        seg.end = 10;
        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(3, uniq.start);
        TEST_ASSERT_EQUAL(5, uniq.end);
        TEST_ASSERT_EQUAL(3, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(10, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next);

        /* Insert 0->10. Expected state after insert: (0->10) */
        seg.start = 0;
        seg.end = 10;
        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(0, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_EQUAL(0, uniq.start);
        TEST_ASSERT_EQUAL(3, uniq.end);
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(10, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next);

        /* Insert 3->10. Expected state after insert: (0->10) (unmodified) */
        seg.start = 3;
        seg.end = 10;
        status = rlc_sdu_seg_insert(&ctx, sdu, &seg, &uniq);
        TEST_ASSERT_EQUAL(-ENODATA, status);
        TEST_ASSERT_FALSE(rlc_segment_okay(&seg));
        TEST_ASSERT_FALSE(rlc_segment_okay(&uniq));
        TEST_ASSERT_EQUAL(0, sdu->segments->seg.start);
        TEST_ASSERT_EQUAL(10, sdu->segments->seg.end);
        TEST_ASSERT_EQUAL_PTR(NULL, sdu->segments->next);

        rlc_sdu_decref(&ctx, sdu);
}

int main(void)
{
        UnityBegin(__FILE__);

        RUN_TEST(test_insert_segment_back);
        RUN_TEST(test_insert_segment_front);

        return UnityEnd();
}
