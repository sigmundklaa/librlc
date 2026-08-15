
#ifndef RLC_TEST_UTIL_BUF_HH__
#define RLC_TEST_UTIL_BUF_HH__

#include <memory>

#include <catch2/catch_all.hpp>

#include <gabs/pbuf.h>
#include <gabs/alloc/std.hh>

#include "util/mem.hh"

namespace rlc::test::util::buf
{

struct pbuf_unref {
        void operator()(::gabs_pbuf *buf)
        {
                ::gabs_pbuf_decref(*buf);
                delete buf;
        }
};

class pbuf_ptr
{
      public:
        pbuf_ptr(::gabs_pbuf buf) : ptr(new ::gabs_pbuf)
        {
                *ptr = buf;
        }

        pbuf_ptr(const pbuf_ptr &) = delete;
        pbuf_ptr(pbuf_ptr &&other)
        {
                std::swap(ptr, other.ptr);
        };

        ::gabs_pbuf weak() const
        {
                return *ptr;
        }

        ::gabs_pbuf strong() const
        {
                ::gabs_pbuf_incref(*ptr);
                return *ptr;
        }

        /*
         * Owning handle over a buffer someone else owns, for reading a
         * buffer that is only borrowed for the duration of a call. The
         * constructor adopts the reference it is given, so this takes one
         * of its own and leaves the lender's untouched.
         */
        static pbuf_ptr from_weak(::gabs_pbuf buf)
        {
                ::gabs_pbuf_incref(buf);

                return pbuf_ptr(buf);
        }

        operator ::gabs_pbuf() const
        {
                return weak();
        }

        operator ::gabs_pbuf *() const
        {
                return ptr.get();
        }

        std::vector<std::byte> vec() const
        {
                std::vector<std::byte> ret;
                ::gabs_pbuf_ci it;

                gabs_pbuf_ci_foreach(ptr.get(), it)
                {
                        auto data = reinterpret_cast<const std::byte *>(
                                ::gabs_pbuf_ci_data(it));
                        ret.insert(ret.end(), data,
                                   data + ::gabs_pbuf_ci_size(it));
                }

                return ret;
        }

        operator std::vector<std::byte>() const
        {
                return vec();
        }

      private:
        mutable std::unique_ptr<::gabs_pbuf, pbuf_unref> ptr;
};

/**
 * @brief Create an empty buffer with @p capacity bytes of tailroom, suitable
 * as an output buffer for functions that append to a pbuf (e.g. via
 * gabs_pbuf_put).
 */
inline pbuf_ptr create(size_t capacity)
{
        auto buf = ::gabs_pbuf_new(mem::alloc, capacity);
        assert(::gabs_pbuf_okay(buf));

        return pbuf_ptr(buf);
}

template <class Iterator> pbuf_ptr create(Iterator begin, Iterator end)
{
        /* Automatic reference counting of the buffer. */
        auto size = (end - begin) * sizeof(typename Iterator::value_type);

        auto buf = ::gabs_pbuf_new(mem::alloc, size);
        assert(::gabs_pbuf_okay(buf));

        ::gabs_pbuf_put(&buf, (const uint8_t *)&*begin, size);

        return pbuf_ptr(buf);
}

template <class Container>
        requires requires(const Container &c) { c.cbegin(); c.cend(); }
pbuf_ptr create(const Container &container)
{
        return create(container.cbegin(), container.cend());
}

template <class Iterator>
class matcher : public Catch::Matchers::MatcherGenericBase
{
      public:
        matcher(Iterator begin, Iterator end) : begin(begin), end(end)
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

                return cmp_it == end;
        }

        bool match(const pbuf_ptr &buf) const
        {
                auto weak_buf = buf.weak();
                return match(weak_buf);
        }

        std::string describe() const override
        {
                auto size = (end - begin);
                size *= sizeof(typename Iterator::value_type);

                return "Equal to sequence of size " + std::to_string(size);
        }

      private:
        Iterator begin;
        Iterator end;
};

template <class Iterator> auto matches_contents(Iterator start, Iterator end)
{
        return matcher(start, end);
}

template <class Container> auto matches_contents(const Container &container)
{
        return matcher(container.begin(), container.end());
}

}; // namespace rlc::test::util::buf

#endif /* RLC_TEST_UTIL_BUF_HH__ * */
