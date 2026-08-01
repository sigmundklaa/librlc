
#include <cerrno>
#include <utility>
#include <vector>

#include <catch2/catch_all.hpp>

#include <rlc/seg_list.h>

#include "util/mem.hh"

namespace rlc::test
{

using namespace util;

namespace
{

using segvec = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

segvec collect(::rlc_seg_list &list)
{
        segvec ret;
        ::rlc_list_it it;

        rlc_list_foreach(&list, it)
        {
                auto *item = ::rlc_seg_item_from_it(it);
                ret.emplace_back(item->seg.start, item->seg.end);
        }

        return ret;
}

} // namespace

TEST_CASE("seg_okay", "[seg_list]")
{
        ::rlc_seg zero = {0, 0};
        ::rlc_seg regular = {0, 5};
        ::rlc_seg reversed = {5, 0};
        ::rlc_seg degenerate = {5, 5};

        REQUIRE(!::rlc_seg_okay(&zero));
        REQUIRE(::rlc_seg_okay(&regular));
        REQUIRE(::rlc_seg_okay(&reversed));
        REQUIRE(::rlc_seg_okay(&degenerate));
}

TEST_CASE("seg list insert - ordering with no overlap", "[seg_list]")
{
        ::rlc_seg_list list;
        ::rlc_list_init(&list);

        ::rlc_seg seg;
        ::rlc_seg unique;
        ::rlc_errno status;

        seg = {8, 12};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE(collect(list) == segvec{{8, 12}});

        /* Non-overlapping insert after the only element appends at the
         * back, leaving a gap. */
        seg = {20, 25};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE(collect(list) ==
               segvec{{8, 12}, {20, 25}});

        /* Non-overlapping insert before the first element prepends. */
        seg = {0, 4};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE(collect(list) ==
               segvec{{0, 4}, {8, 12}, {20, 25}});

        /* Non-overlapping insert between two existing, non-adjacent
         * elements. */
        seg = {14, 16};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE(collect(list) == segvec{
                                         {0, 4}, {8, 12}, {14, 16}, {20, 25}});

        ::rlc_seg_list_clear(&list, mem::alloc);
        REQUIRE(list.head == NULL);
}

TEST_CASE("seg list insert - fully contained segment is rejected",
         "[seg_list]")
{
        ::rlc_seg_list list;
        ::rlc_list_init(&list);

        ::rlc_seg seg = {5, 10};
        ::rlc_seg unique;
        ::rlc_errno status;

        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);

        SECTION("strictly contained within an existing segment")
        {
                seg = {6, 9};
        }

        SECTION("exact duplicate of an existing segment")
        {
                seg = {5, 10};
        }

        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == -ENODATA);
        REQUIRE(seg.start == 0);
        REQUIRE(seg.end == 0);
        REQUIRE(unique.start == 0);
        REQUIRE(unique.end == 0);

        /* List must be left completely unchanged. */
        REQUIRE(collect(list) == segvec{{5, 10}});

        ::rlc_seg_list_clear(&list, mem::alloc);
}

TEST_CASE("seg list insert - partial overlap merges into a single "
         "neighbour",
         "[seg_list]")
{
        ::rlc_seg_list list;
        ::rlc_list_init(&list);

        ::rlc_seg seg;
        ::rlc_seg unique;
        ::rlc_errno status;

        seg = {0, 5};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);

        seg = {20, 25};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);

        SECTION("overlap on the left segment only extends it, no new node")
        {
                seg = {3, 15};
                status = ::rlc_seg_list_insert(&list, &seg, &unique,
                                               mem::alloc);
                REQUIRE(status == 0);
                REQUIRE(unique.start == 5);
                REQUIRE(unique.end == 15);
                REQUIRE(collect(list) ==
                       segvec{{0, 15}, {20, 25}});
        }

        SECTION("overlap on the right segment only extends it, no new node")
        {
                seg = {10, 22};
                status = ::rlc_seg_list_insert(&list, &seg, &unique,
                                               mem::alloc);
                REQUIRE(status == 0);
                REQUIRE(unique.start == 10);
                REQUIRE(unique.end == 20);
                REQUIRE(collect(list) ==
                       segvec{{0, 5}, {10, 25}});
        }

        ::rlc_seg_list_clear(&list, mem::alloc);
}

TEST_CASE("seg list insert - overlap on both sides bridges and merges "
         "two segments, freeing the redundant node",
         "[seg_list]")
{
        ::rlc_seg_list list;
        ::rlc_list_init(&list);

        ::rlc_seg seg;
        ::rlc_seg unique;
        ::rlc_errno status;

        seg = {0, 5};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);

        seg = {10, 15};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);

        seg = {3, 12};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);
        REQUIRE(unique.start == 5);
        REQUIRE(unique.end == 10);
        REQUIRE(collect(list) == segvec{{0, 15}});

        ::rlc_seg_list_clear(&list, mem::alloc);
}

TEST_CASE("seg list insert_all bridges multiple islands and gaps",
         "[seg_list]")
{
        ::rlc_seg_list list;
        ::rlc_list_init(&list);

        ::rlc_seg seg;
        ::rlc_seg unique;
        ::rlc_errno status;

        seg = {0, 5};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);

        seg = {10, 15};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);

        seg = {20, 25};
        status = ::rlc_seg_list_insert(&list, &seg, &unique, mem::alloc);
        REQUIRE(status == 0);

        REQUIRE(collect(list) ==
               segvec{{0, 5}, {10, 15}, {20, 25}});

        SECTION("segment spanning both gaps and all three islands merges "
               "everything into one, across multiple internal passes")
        {
                seg = {2, 23};
                status = ::rlc_seg_list_insert_all(&list, seg, mem::alloc);
                REQUIRE(status == 0);
                REQUIRE(collect(list) == segvec{{0, 25}});
        }

        SECTION("segment fully contained in an island is rejected as a "
               "single pass")
        {
                seg = {11, 14};
                status = ::rlc_seg_list_insert_all(&list, seg, mem::alloc);
                REQUIRE(status == -ENODATA);
                REQUIRE(collect(list) ==
                       segvec{{0, 5}, {10, 15}, {20, 25}});
        }

        SECTION("disjoint segment needs only a single pass")
        {
                seg = {30, 35};
                status = ::rlc_seg_list_insert_all(&list, seg, mem::alloc);
                REQUIRE(status == 0);
                REQUIRE(collect(list) == segvec{
                                                 {0, 5},
                                                 {10, 15},
                                                 {20, 25},
                                                 {30, 35}});
        }

        ::rlc_seg_list_clear(&list, mem::alloc);
}

TEST_CASE("seg list clear_until_last", "[seg_list]")
{
        ::rlc_seg_list list;
        ::rlc_list_init(&list);

        SECTION("empty list is a no-op")
        {
                ::rlc_seg_list_clear_until_last(&list, mem::alloc);
                REQUIRE(list.head == NULL);
        }

        SECTION("single-element list is a no-op")
        {
                ::rlc_seg seg = {5, 10};
                ::rlc_seg unique;

                REQUIRE(::rlc_seg_list_insert(&list, &seg, &unique,
                                              mem::alloc) == 0);

                ::rlc_seg_list_clear_until_last(&list, mem::alloc);
                REQUIRE(collect(list) == segvec{{5, 10}});
        }

        SECTION("multi-element list keeps only the last element")
        {
                ::rlc_seg segs[] = {{0, 5}, {10, 15}, {20, 25}};
                ::rlc_seg unique;

                for (auto s : segs) {
                        REQUIRE(::rlc_seg_list_insert(&list, &s, &unique,
                                                      mem::alloc) == 0);
                }

                ::rlc_seg_list_clear_until_last(&list, mem::alloc);
                REQUIRE(collect(list) == segvec{{20, 25}});
        }

        ::rlc_seg_list_clear(&list, mem::alloc);
}

TEST_CASE("seg list clear", "[seg_list]")
{
        ::rlc_seg_list list;
        ::rlc_list_init(&list);

        SECTION("empty list is a no-op")
        {
        }

        SECTION("multi-element list is fully emptied")
        {
                ::rlc_seg segs[] = {{0, 5}, {10, 15}, {20, 25}};
                ::rlc_seg unique;

                for (auto s : segs) {
                        REQUIRE(::rlc_seg_list_insert(&list, &s, &unique,
                                                      mem::alloc) == 0);
                }
        }

        ::rlc_seg_list_clear(&list, mem::alloc);
        REQUIRE(list.head == NULL);
}

}; // namespace rlc::test
