
#include <rlc/buf.h>
#include <rlc/plat.h>

#include "ptest.h"

static rlc_context ctx;

#if defined(RLC_PLAT_ZEPHYR)
NET_BUF_POOL_HEAP_DEFINE(zbuf_pool, 100, 0, NULL);
#endif

static void *harness_alloc(struct rlc_context *c, size_t size,
                           enum rlc_alloc_type type)
{
        switch (type) {
        case RLC_ALLOC_BUF:
#if defined(RLC_PLAT_ZEPHYR)
                return net_buf_alloc_len(&zbuf_pool, size, K_NO_WAIT);
#endif
                size += sizeof(rlc_buf);
                break;
        default:
                break;
        }

        return malloc(size);
}

static void harness_dealloc(struct rlc_context *ctx, void *mem,
                            enum rlc_alloc_type type)
{
#if defined(RLC_PLAT_ZEPHYR)
        if (type == RLC_ALLOC_BUF) {
                net_buf_unref(mem);
                return;
        }
#endif
        free(mem);
}

static void setup(void)
{
        static const struct rlc_methods methods = {
                .event = NULL,
                .mem_alloc = harness_alloc,
                .mem_dealloc = harness_dealloc,
                .tx_request = NULL,
                .tx_submit = NULL,
        };
        static const struct rlc_config conf;

        rlc_errno status;

        status = rlc_init(&ctx, RLC_AM, &conf, &methods, NULL);
        rlc_assert(status == 0);
}

static void teardown(void)
{
        rlc_errno status;

        status = rlc_deinit(&ctx);
        rlc_assert(status == 0);
}

ptest_suite(setup, teardown);

#define bufput(ptr, data) rlc_buf_put(ptr, data, sizeof(data))

#define dataarea(sz)                                                           \
        struct {                                                               \
                char mem[sz];                                                  \
                int offset;                                                    \
        } data__ = {.mem = {0}, .offset = 0};

#if defined(RLC_PLAT_ZEPHYR)

#define nextptr(buf) buf.frags

#define bufinit(name_, cap_)                                                   \
        rlc_buf name_ = {0};                                                   \
        do {                                                                   \
                name_.data = (void *)(data__.mem + data__.offset);             \
                name_.size = cap_;                                             \
                data__.offset += cap_;                                         \
        } while (0)

#elif defined(RLC_PLAT_LINUX)

#define nextptr(buf) buf.next

#define bufinit(name_, cap_)                                                   \
        rlc_buf name_ = {0};                                                   \
        do {                                                                   \
                name_.data = (void *)(data__.mem + data__.offset);             \
                name_.cap = cap_;                                              \
                data__.offset += cap_;                                         \
        } while (0)

#else
#error "Unrecognized architecture"
#endif

ptest_function(test_buffer_chain_at)
{
        dataarea(100);

        rlc_buf *ptr;

        bufinit(buf, 4);
        bufinit(one, 4);
        bufinit(two, 4);
        bufinit(three, 6);
        bufinit(four, 5);

        bufput(&buf, "buf");
        bufput(&one, "one");
        bufput(&two, "two");
        bufput(&three, "three");
        bufput(&four, "four");

        ptr = rlc_buf_chain_at(&buf, &one, 4);
        ptest_assert_eq(&buf, ptr);
        ptest_assert_eq(&one, nextptr(buf));
        ptest_assert_eq(NULL, nextptr(one));

        ptr = rlc_buf_chain_at(&buf, &two, 8);
        ptest_assert_eq(&buf, ptr);
        ptest_assert_eq(&one, nextptr(buf));
        ptest_assert_eq(&two, nextptr(one));
        ptest_assert_eq(NULL, nextptr(two));

        ptr = rlc_buf_chain_at(&buf, &three, 4);
        ptest_assert_eq(&buf, ptr);
        ptest_assert_eq(&three, nextptr(buf));
        ptest_assert_eq(&one, nextptr(three));
        ptest_assert_eq(&two, nextptr(one));
        ptest_assert_eq(NULL, nextptr(two));

        ptr = rlc_buf_chain_at(&buf, &four, 10);
        ptest_assert_eq(&buf, ptr);
        ptest_assert_eq(&three, nextptr(buf));
        ptest_assert_eq(&four, nextptr(three));
        ptest_assert_eq(&one, nextptr(four));
        ptest_assert_eq(&two, nextptr(one));
        ptest_assert_eq(NULL, nextptr(two));
}

ptest_function(test_buffer_chain_front)
{
        dataarea(100);

        rlc_buf *ptr;

        bufinit(buf, 4);
        bufinit(one, 4);
        bufinit(two, 4);

        bufput(&buf, "buf");
        bufput(&one, "one");
        bufput(&two, "two");

        ptr = rlc_buf_chain_front(&buf, &one);
        ptest_assert_eq(&one, ptr);
        ptest_assert_eq(&buf, nextptr(one));
        ptest_assert_eq(NULL, nextptr(buf));

        ptr = rlc_buf_chain_front(ptr, &two);
        ptest_assert_eq(&two, ptr);
        ptest_assert_eq(&one, nextptr(two));
        ptest_assert_eq(&buf, nextptr(one));
        ptest_assert_eq(NULL, nextptr(buf));
}

ptest_function(test_buffer_chain_back)
{
        dataarea(100);

        rlc_buf *ptr;

        bufinit(buf, 4);
        bufinit(one, 4);
        bufinit(two, 4);

        bufput(&buf, "buf");
        bufput(&one, "one");
        bufput(&two, "two");

        ptr = rlc_buf_chain_back(&buf, &one);
        ptest_assert_eq(&buf, ptr);
        ptest_assert_eq(&one, nextptr(buf));
        ptest_assert_eq(NULL, nextptr(one));

        ptr = rlc_buf_chain_back(ptr, &two);
        ptest_assert_eq(&buf, ptr);
        ptest_assert_eq(&one, nextptr(buf));
        ptest_assert_eq(&two, nextptr(one));
        ptest_assert_eq(NULL, nextptr(two));
}

ptest_function(test_buffer_strip_front)
{
        rlc_buf *one;
        rlc_buf *two;
        rlc_buf *ptr;
        size_t ret;

        one = rlc_buf_alloc(&ctx, 6);
        two = rlc_buf_alloc(&ctx, 5);

        rlc_assert(one != NULL);
        rlc_assert(two != NULL);

        bufput(one, "12345");
        bufput(two, "6789");

        rlc_buf_incref(two);
        (void)rlc_buf_chain_back(one, two);

        ptest_assert_eq(11, rlc_buf_size(one));

        /* Strip first 4 (of 6) bytes of one */
        ptr = rlc_buf_strip_front(one, 4, &ctx);
        ptest_assert_eq(one, ptr);
        ptest_assert_eq(7, rlc_buf_size(one));

        char copied1[rlc_buf_size(one)];
        ret = rlc_buf_copy(one, copied1, 0, rlc_buf_size(one));
        ptest_assert_eq(rlc_buf_size(one), ret);
        ptest_assert_eq_mem("5\0"
                            "6789",
                            copied1, rlc_buf_size(one));

        /* Strip last 2. Should still return `one`, although empty */
        ptr = rlc_buf_strip_front(one, 2, &ctx);
        ptest_assert_eq(one, ptr);
        ptest_assert_eq(rlc_buf_size(two), rlc_buf_size(one));

        char copied2[rlc_buf_size(one)];
        ret = rlc_buf_copy(one, copied2, 0, rlc_buf_size(one));
        ptest_assert_eq(rlc_buf_size(one), ret);
        ptest_assert_eq_mem("6789", copied2, 5);

        /* Strip one more. This should remove the first one. */
        ptr = rlc_buf_strip_front(one, 1, &ctx);
        ptest_assert_eq(two, ptr);
        ptest_assert_eq(4, rlc_buf_size(ptr));

        char copied3[4];
        ret = rlc_buf_copy(ptr, copied3, 0, rlc_buf_size(ptr));
        ptest_assert_eq(rlc_buf_size(ptr), ret);
        ptest_assert_eq_mem("789", copied3, 4);

        rlc_buf_decref(ptr, &ctx);
}

ptest_function(test_buffer_strip_back)
{
        rlc_buf *one;
        rlc_buf *two;
        rlc_buf *ptr;
        size_t ret;

        one = rlc_buf_alloc(&ctx, 6);
        two = rlc_buf_alloc(&ctx, 5);

        rlc_assert(one != NULL);
        rlc_assert(two != NULL);

        bufput(one, "12345");
        bufput(two, "6789");

        ptr = rlc_buf_chain_back(two, one);
        ptest_assert_eq(two, ptr);

        ptest_assert_eq(11, rlc_buf_size(ptr));

        /* Strip first 4 (of 6) bytes of one */
        ptr = rlc_buf_strip_back(ptr, 4, &ctx);
        ptest_assert_eq(two, ptr);
        ptest_assert_eq(7, rlc_buf_size(ptr));

        char copied1[rlc_buf_size(ptr)];
        ret = rlc_buf_copy(ptr, copied1, 0, rlc_buf_size(ptr));
        ptest_assert_eq(rlc_buf_size(ptr), ret);
        ptest_assert_eq_mem("6789\0"
                            "12",
                            copied1, rlc_buf_size(ptr));

        /* Strip last 2. Should still return `two`, although empty */
        ptr = rlc_buf_strip_back(ptr, 2, &ctx);
        ptest_assert_eq(two, ptr);
        ptest_assert_eq(rlc_buf_size(0), rlc_buf_size(one));

        char copied2[rlc_buf_size(ptr)];
        ret = rlc_buf_copy(ptr, copied2, 0, rlc_buf_size(ptr));
        ptest_assert_eq(rlc_buf_size(ptr), ret);
        ptest_assert_eq_mem("6789", copied2, 5);

        /* Strip one more. This should remove the first one. */
        ptr = rlc_buf_strip_front(ptr, 1, &ctx);
        ptest_assert_eq(two, ptr);
        ptest_assert_eq(4, rlc_buf_size(ptr));

        char copied3[4];
        ret = rlc_buf_copy(ptr, copied3, 0, rlc_buf_size(ptr));
        ptest_assert_eq(rlc_buf_size(ptr), ret);
        ptest_assert_eq_mem("789", copied3, 4);

        rlc_buf_decref(ptr, &ctx);
}

ptest_function(test_buffer_view)
{
        rlc_buf *one;
        rlc_buf *two;
        rlc_buf *three;
        rlc_buf *four;
        rlc_buf *five;
        rlc_buf *ptr;

        one = rlc_buf_alloc(&ctx, 2);
        two = rlc_buf_alloc(&ctx, 3);
        three = rlc_buf_alloc(&ctx, 4);
        four = rlc_buf_alloc(&ctx, 5);
        five = rlc_buf_alloc(&ctx, 6);

        rlc_assert(one != NULL);
        rlc_assert(two != NULL);
        rlc_assert(three != NULL);
        rlc_assert(four != NULL);
        rlc_assert(five != NULL);

        bufput(one, "1");
        bufput(two, "22");
        bufput(three, "333");
        bufput(four, "4444");
        bufput(five, "55555");

        ptr = one;
        ptr = rlc_buf_chain_back(ptr, two);
        ptr = rlc_buf_chain_back(ptr, three);
        ptr = rlc_buf_chain_back(ptr, four);
        ptr = rlc_buf_chain_back(ptr, five);

        rlc_buf *view = rlc_buf_view(ptr, 3, 9, &ctx);
        ptest_assert_neq(NULL, view);
        ptest_assert_eq(9, rlc_buf_size(view));

        char data[9];
        size_t ret = rlc_buf_copy(view, data, 0, rlc_buf_size(view));
        ptest_assert_eq(rlc_buf_size(view), ret);

        ptest_assert_eq_mem("2\0"
                            "333\0"
                            "444",
                            data, sizeof(data));

        rlc_buf_decref(view, &ctx);
        rlc_buf_decref(ptr, &ctx);
}

#if defined(RLC_PLAT_LINUX)
int main(void)
{
        UnityBegin(__FILE__);

        RUN_TEST(test_buffer_chain_at);
        RUN_TEST(test_buffer_chain_front);
        RUN_TEST(test_buffer_chain_back);
        RUN_TEST(test_buffer_strip_front);
        RUN_TEST(test_buffer_strip_back);
        RUN_TEST(test_buffer_view);

        return UnityEnd();
}
#endif
