
#include <catch2/catch_all.hpp>

#include <gabs/pbuf.h>
#include <gabs/alloc/std.hh>

#include <rlc/rlc.h>
#include <rlc/seg_buf.h>

#include "util/buf.hh"

namespace rlc::test
{

using namespace util;

TEST_CASE("segment buffer", "[seg_buf]")
{
        ::rlc_errno status;
        ::rlc_seg_buf buf = {0};

        std::string test_str = "hello world";

        ::rlc_seg seg = {8, 12};
        ::rlc_seg uniq;
        status = ::rlc_seg_buf_insert(&buf, buf::create(std::string("89ab")),
                                      seg, mem::alloc, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE_THAT(buf.buf, buf::matches_contents(std::string("89ab")));

        seg.start = 13;
        seg.end = 16;
        status = ::rlc_seg_buf_insert(&buf, buf::create(std::string("def")),
                                      seg, mem::alloc, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE_THAT(buf.buf, buf::matches_contents(std::string("89abdef")));

        seg.start = 0;
        seg.end = 8;
        status =
                ::rlc_seg_buf_insert(&buf, buf::create(std::string("01234567")),
                                     seg, mem::alloc, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE_THAT(buf.buf,
                     buf::matches_contents(std::string("0123456789abdef")));

        seg.start = 8;
        seg.end = 16;
        status =
                ::rlc_seg_buf_insert(&buf, buf::create(std::string("89abcdef")),
                                     seg, mem::alloc, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE_THAT(buf.buf,
                     buf::matches_contents(std::string("0123456789abcdef")));

        /* Re-inserting a segment that's already fully covered is a silent
         * no-op: rlc_seg_list_insert's -ENODATA is swallowed and translated
         * to success, and the buffer content is left untouched. */
        seg.start = 8;
        seg.end = 12;
        status = ::rlc_seg_buf_insert(&buf, buf::create(std::string("XXXX")),
                                      seg, mem::alloc, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE_THAT(buf.buf,
                     buf::matches_contents(std::string("0123456789abcdef")));

        ::rlc_seg_buf_destroy(&buf, mem::alloc);
}

}; // namespace rlc::test
