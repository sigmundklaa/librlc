
#include <catch2/catch_all.hpp>

#include <gabs/pbuf.h>
#include <gabs/alloc/std.hh>

#include <rlc/rlc.h>
#include <rlc/sdu.h>

#include "rx.c"

inline gabs::memory::allocator alloc;

namespace
{

struct pbuf_unref {
        void operator()(::gabs_pbuf *buf)
        {
                ::gabs_pbuf_decref(*buf);
                delete buf;
        }
};

using pbuf_pointer = std::unique_ptr<::gabs_pbuf, pbuf_unref>;

template <class Iterator> pbuf_pointer buf_create(Iterator begin, Iterator end)
{
        /* Automatic reference counting of the buffer. */
        auto size = (end - begin) * sizeof(typename Iterator::value_type);
        pbuf_pointer ptr(new ::gabs_pbuf);

        *ptr = ::gabs_pbuf_new(alloc, size);
        assert(::gabs_pbuf_okay(*ptr));

        ::gabs_pbuf_put(ptr.get(), (const uint8_t *)&*begin, size);
        return std::move(ptr);
}

template <class Container> pbuf_pointer buf_create(const Container &container)
{
        return buf_create(container.cbegin(), container.cend());
}

template <class Iterator>
class buf_matcher : public Catch::Matchers::MatcherGenericBase
{
      public:
        buf_matcher(Iterator begin, Iterator end) : begin(begin), end(end)
        {
        }

        bool match(::gabs_pbuf &buf) const
        {
                ::gabs_pbuf_ci it;
                Iterator cmp_it = begin;

                gabs_pbuf_ci_foreach(&buf, it)
                {
                        auto data = ::gabs_pbuf_ci_data(it);
                        auto size = ::gabs_pbuf_ci_size(it);

                        auto count = size;
                        count /= sizeof(typename Iterator::value_type);

                        if (cmp_it + count > end) {
                                return false;
                        }

                        if (std::memcmp(&*cmp_it, data, size) != 0) {
                                return false;
                        }

                        cmp_it += count;
                }

                return true;
        }

        bool match(const pbuf_pointer &buf) const
        {
                return match(*buf);
        }

        std::string describe() const override
        {
                auto size = (end - begin);
                size *= sizeof(typename Iterator::value_type);

                return "Equal to sequence of size " + std::to_string(size) +
                       ": " + std::string(begin, end);
        }

      private:
        Iterator begin;
        Iterator end;
};

template <class Iterator> auto matches_contents(Iterator start, Iterator end)
{
        return buf_matcher(start, end);
}

template <class Container> auto matches_contents(const Container &container)
{
        return buf_matcher(container.begin(), container.end());
}

}; // namespace

TEST_CASE("rx buffer insertion", "[rx]")
{
        ::rlc_errno status;
        ::rlc_sdu sdu;
        ::rlc_context ctx;
        std::memset(&sdu, 0, sizeof(sdu));

        ctx.alloc_misc = alloc;
        ctx.alloc_buf = alloc;

        std::string test_str = "hello world";

        ::rlc_segment seg = {8, 12};
        ::rlc_segment uniq;
        status = ::insert_buffer(&ctx, &sdu,
                                 buf_create(std::string("89ab")).get(), seg);
        REQUIRE(status == 0);
        REQUIRE_THAT(sdu.buffer, matches_contents(std::string("89ab")));

        seg.start = 13;
        seg.end = 16;
        status = ::insert_buffer(&ctx, &sdu,
                                 buf_create(std::string("def")).get(), seg);
        REQUIRE(status == 0);
        REQUIRE_THAT(sdu.buffer, matches_contents(std::string("89abdef")));

        seg.start = 0;
        seg.end = 8;
        status = ::insert_buffer(
                &ctx, &sdu, buf_create(std::string("01234567")).get(), seg);
        REQUIRE(status == 0);
        REQUIRE_THAT(sdu.buffer,
                     matches_contents(std::string("0123456789abdef")));

        seg.start = 8;
        seg.end = 16;
        status = ::insert_buffer(
                &ctx, &sdu, buf_create(std::string("89abcdef")).get(), seg);
        REQUIRE(status == 0);
        REQUIRE_THAT(sdu.buffer,
                     matches_contents(std::string("0123456789abcdef")));
}
