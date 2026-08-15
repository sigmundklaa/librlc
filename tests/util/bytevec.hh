
#ifndef RLC_TEST_UTIL_BYTEVEC_HH__
#define RLC_TEST_UTIL_BYTEVEC_HH__

#include <algorithm>
#include <cstddef>
#include <vector>

namespace rlc::test::util
{

/*
 * Restates a container of byte-sized elements - a std::string literal in a
 * test, a std::vector<std::uint8_t> - as the std::vector<std::byte> that
 * pbuf_ptr::vec() and the proto encoders deal in, so the two can be
 * compared directly.
 */
template <class Container>
std::vector<std::byte> to_bytevec(const Container &c)
{
        std::vector<std::byte> ret(c.size());

        std::transform(c.begin(), c.end(), ret.begin(),
                      [](auto v) { return static_cast<std::byte>(v); });

        return ret;
}

} // namespace rlc::test::util

#endif /* RLC_TEST_UTIL_BYTEVEC_HH__ */
