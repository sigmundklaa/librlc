
#ifndef PTEST_H__
#define PTEST_H__

#include <rlc/rlc.h>

#if defined(RLC_PLAT_ZEPHYR)

#include <zephyr/ztest.h>

#define ptest_suite(setup_, teardown_)                                         \
        static void setup_wrapper__(void *arg)                                 \
        {                                                                      \
                ARG_UNUSED(arg);                                               \
                setup_();                                                      \
        }                                                                      \
        static void teardown_wrapper__(void *arg)                              \
        {                                                                      \
                ARG_UNUSED(arg);                                               \
                teardown_();                                                   \
        }                                                                      \
        ZTEST_SUITE(ptest, NULL, NULL, setup_wrapper__, teardown_wrapper__,    \
                    NULL);

#define ptest_function(name_) ZTEST(ptest, name_)

#define ptest_assert_eq(exp_, real_)  zassert_equal(exp_, real_)
#define ptest_assert_neq(exp_, real_) zassert_not_equal(exp_, real_)

#define ptest_assert_eq_mem(exp_, real_, sz_)                                  \
        zassert_mem_equal(exp_, real_, sz_)

#elif defined(RLC_PLAT_LINUX)

#include <unity/unity.h>

#define ptest_suite(setup_, teardown_)                                         \
        void setUp(void)                                                       \
        {                                                                      \
                setup_();                                                      \
        }                                                                      \
        void tearDown(void)                                                    \
        {                                                                      \
                teardown_();                                                   \
        }

#define ptest_function(name_) static void name_(void)

#define ptest_assert_eq(exp_, real_)  TEST_ASSERT_EQUAL(exp_, real_)
#define ptest_assert_neq(exp_, real_) TEST_ASSERT_NOT_EQUAL(exp_, real_)

#define ptest_assert_eq_mem(exp_, real_, sz_)                                  \
        TEST_ASSERT_EQUAL_CHAR_ARRAY(exp_, real_, sz_)

#else
#error "Unrecognized architecture"
#endif /* Platform */

#endif /* PTEST_H__ */
