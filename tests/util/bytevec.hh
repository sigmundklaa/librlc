
#ifndef RLC_TEST_UTIL_BYTEVEC_HH__
#define RLC_TEST_UTIL_BYTEVEC_HH__

#include <algorithm>
#include <cstddef>
#include <vector>

namespace rlc::test::util
{

/* Convert a container of byte-sized elements to the std::vector<std::byte>
 * that pbuf_ptr::vec() and the proto encoders return. */
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
