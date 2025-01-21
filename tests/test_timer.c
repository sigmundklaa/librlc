
#include <unity/unity.h>
#include <rlc/timer.h>

void setUp(void)
{
        rlc_plat_init();
}

void tearDown(void)
{
}

void cb_one(rlc_timer timer, void *arg)
{
        TEST_ASSERT_EQUAL_PTR(0x1234, arg);

        (void)printf("hey\n");

        rlc_timer_restart(timer, 1e6);
}

void cb_two(rlc_timer timer, void *arg)
{
        (void)printf("ho\n");

        rlc_timer_restart(timer, 3e5);
}

static void test_timer(void)
{
        rlc_timer timer1;
        rlc_timer timer2;
        rlc_errno status;

        timer1 = rlc_timer_install(cb_one, (void *)0x1234);
        TEST_ASSERT_TRUE(rlc_timer_okay(timer1));

        status = rlc_timer_start(timer1, 2e5);
        TEST_ASSERT_EQUAL(0, status);

        timer2 = rlc_timer_install(cb_two, (void *)0x5679);
        TEST_ASSERT_TRUE(rlc_timer_okay(timer2));

        status = rlc_timer_start(timer2, 5e5);
        TEST_ASSERT_EQUAL(0, status);
}

int main(void)
{
        UnityBegin(__FILE__);

        RUN_TEST(test_timer);

        sleep(5);

        return UnityEnd();
}
