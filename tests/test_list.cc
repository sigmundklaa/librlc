
#include <vector>
#include <sstream>

#include <catch2/catch_all.hpp>

#include <rlc/list.h>

namespace
{

template <typename T> class list_item
{
      public:
        using value_type = T;

        template <typename... Args> list_item(Args... args) : inner(args...)
        {
                ::rlc_list_node_init(&node);
        }

        const T &get() const
        {
                return inner;
        }

        operator T()
        {
                return inner;
        }

        operator const T() const
        {
                return inner;
        }

        operator ::rlc_list_node *()
        {
                return &node;
        }

        operator const ::rlc_list_node *() const
        {
                return &node;
        }

        bool operator==(const T &other) const
        {
                return inner == other;
        }

        bool operator==(::rlc_list_node *other) const
        {
                return &node == other;
        }

        bool operator==(const ::rlc_list_node *other) const
        {
                return &node == other;
        }

        bool operator==(::rlc_list_it it) const
        {
                return operator==(::rlc_list_it_node(it));
        }

        static list_item<T> &from(::rlc_list_it it)
        {
                auto ptr = rlc_list_it_item(it, list_item<T>, node);
                return *ptr;
        }

      private:
        T inner;
        ::rlc_list_node node;
};

template <typename T, class Iterator>
class list_matcher : public Catch::Matchers::MatcherGenericBase
{
      public:
        list_matcher(Iterator start, Iterator end) : start(start), end(end)
        {
        }

        bool match(::rlc_list_it it) const
        {
                auto vecit = start;

                for (; !::rlc_list_it_eoi(it); it = ::rlc_list_it_next(it)) {
                        assert(vecit < end);

                        if (!(list_item<T>::from(it) == *vecit)) {
                                return false;
                        }

                        vecit++;
                }

                return true;
        }

        bool match(::rlc_list &list) const
        {
                return match(::rlc_list_it_init(&list));
        }

        std::string describe() const override
        {
                std::stringstream ss;

                ss << "Equal vector of size " << end - start
                   << ", with elements {";

                for (auto it = start; it < end; it++) {
                        ss << std::to_string(*it) << ",";
                }

                ss << "}";

                return ss.str();
        }

      private:
        Iterator start;
        Iterator end;
};

template <typename Iterator> auto matches_list(Iterator start, Iterator end)
{
        using value_type = typename std::iterator_traits<Iterator>::value_type;
        return list_matcher<value_type, Iterator>(start, end);
}

template <typename T> auto matches_list(const std::vector<T> &list)
{
        using iterator = typename std::vector<T>::const_iterator;
        return list_matcher<T, iterator>(list.cbegin(), list.cend());
}

bool cmp_it(::rlc_list_it a, ::rlc_list_it b)
{
        return a.node == b.node && a.slotptr == b.slotptr;
}

}; // namespace

TEST_CASE("list iterator", "[list]")
{
        std::vector<std::uint32_t> truth;
        std::vector<list_item<std::uint32_t>> storage;
        ::rlc_list list;

        ::rlc_list_init(&list);
        REQUIRE(list.head == NULL);

        for (auto i = 0; i < 10; i++) {
                truth.push_back(i);
                storage.push_back(i);
        }

        ::rlc_list_it it = ::rlc_list_it_init(&list);

        for (auto &obj : storage) {
                it = ::rlc_list_it_put_back(it, obj);
        }

        REQUIRE_THAT(list, matches_list(truth));

        SECTION("pop/put front")
        {
                ::rlc_list_it it = ::rlc_list_it_init(&list);

                REQUIRE(storage[0] == ::rlc_list_it_node(it));

                it = ::rlc_list_it_pop(it, NULL);
                REQUIRE(storage[1] == ::rlc_list_it_node(it));
                REQUIRE(cmp_it(it, ::rlc_list_it_next(it)));
                REQUIRE(storage[1] ==
                        ::rlc_list_it_node(::rlc_list_it_init(&list)));

                /* Reset iterator to head */
                it = ::rlc_list_it_init(&list);

                REQUIRE(storage[1] == ::rlc_list_it_node(it));

                it = ::rlc_list_it_put_front(it, storage[0]);
                REQUIRE(storage[1] == ::rlc_list_it_node(it));
                REQUIRE(storage[2] ==
                        ::rlc_list_it_node(::rlc_list_it_next(it)));
                REQUIRE(storage[0] ==
                        ::rlc_list_it_node(::rlc_list_it_init(&list)));
        }

        SECTION("remove within")
        {
                auto &item = storage[2];
                ::rlc_list_it it;

                rlc_list_foreach(&list, it)
                {
                        if (item == ::rlc_list_it_node(it)) {
                                break;
                        }
                }

                REQUIRE(!::rlc_list_it_eoi(it));

                it = ::rlc_list_it_pop(it, NULL);
                truth.erase(truth.begin() + 2);

                REQUIRE(::rlc_list_it_node(it) ==
                        ::rlc_list_it_node(::rlc_list_it_next(it)));
                REQUIRE_THAT(::rlc_list_it_next(it),
                             matches_list(truth.cbegin() + 2, truth.cend()));
                REQUIRE_THAT(list, matches_list(truth));
        }

        SECTION("remove consecutive")
        {

                ::rlc_list_it it;
                auto storageit = storage.begin();

                it = ::rlc_list_it_init(&list);

                for (auto i = 0; i < storage.size(); i++) {
                        REQUIRE(!::rlc_list_it_eoi(it));
                        REQUIRE(*storageit == it);

                        it = ::rlc_list_it_pop(it, NULL);
                        storageit++;

                        REQUIRE(::rlc_list_it_node(it) ==
                                ::rlc_list_it_node(::rlc_list_it_next(it)));
                        REQUIRE(::rlc_list_it_node(::rlc_list_it_init(&list)) ==
                                ::rlc_list_it_node(it));

                        it = ::rlc_list_it_next(it);
                }

                REQUIRE(::rlc_list_it_eoi(it));
        }
}

TEST_CASE("list iterator - empty list", "[list]")
{
        ::rlc_list list;

        ::rlc_list_init(&list);
        REQUIRE(list.head == NULL);

        ::rlc_list_it it = ::rlc_list_it_init(&list);
        REQUIRE(::rlc_list_it_eoi(it));
        REQUIRE(::rlc_list_it_node(it) == NULL);

        /* Advancing an already-at-end iterator is a no-op */
        it = ::rlc_list_it_next(it);
        REQUIRE(::rlc_list_it_eoi(it));

        int count = 0;
        rlc_list_foreach(&list, it)
        {
                count++;
        }
        REQUIRE(count == 0);

        /* put_back on an empty list's iterator inserts the first element */
        list_item<std::uint32_t> only(42);

        it = ::rlc_list_it_init(&list);
        it = ::rlc_list_it_put_back(it, only);
        REQUIRE_THAT(list, matches_list(std::vector<std::uint32_t>{42}));

        /* Popping the sole remaining element empties the list again */
        it = ::rlc_list_it_init(&list);
        it = ::rlc_list_it_pop(it, NULL);
        REQUIRE(list.head == NULL);
        REQUIRE(::rlc_list_it_eoi(::rlc_list_it_init(&list)));
}

TEST_CASE("list iterator - insert in the middle", "[list]")
{
        std::vector<std::uint32_t> truth = {0, 1, 2};
        std::vector<list_item<std::uint32_t>> storage;

        for (auto v : truth) {
                storage.emplace_back(v);
        }

        ::rlc_list list;
        ::rlc_list_init(&list);

        ::rlc_list_it it = ::rlc_list_it_init(&list);
        for (auto &obj : storage) {
                it = ::rlc_list_it_put_back(it, obj);
        }

        REQUIRE_THAT(list, matches_list(truth));

        SECTION("put_back after the head inserts immediately following it")
        {
                list_item<std::uint32_t> extra(99);

                ::rlc_list_it head_it = ::rlc_list_it_init(&list);
                ::rlc_list_it_put_back(head_it, extra);

                std::vector<std::uint32_t> expect = {0, 99, 1, 2};
                REQUIRE_THAT(list, matches_list(expect));
        }

        SECTION("put_front before the last element inserts immediately "
               "preceding it")
        {
                list_item<std::uint32_t> extra(99);
                ::rlc_list_it target;

                rlc_list_foreach(&list, target)
                {
                        if (storage[2] == ::rlc_list_it_node(target)) {
                                break;
                        }
                }
                REQUIRE(!::rlc_list_it_eoi(target));

                ::rlc_list_it_put_front(target, extra);

                std::vector<std::uint32_t> expect = {0, 1, 99, 2};
                REQUIRE_THAT(list, matches_list(expect));
        }
}

TEST_CASE("list iterator - skip and repeat", "[list]")
{
        std::vector<std::uint32_t> truth = {0, 1, 2, 3};
        std::vector<list_item<std::uint32_t>> storage;

        for (auto v : truth) {
                storage.emplace_back(v);
        }

        ::rlc_list list;
        ::rlc_list_init(&list);

        ::rlc_list_it it = ::rlc_list_it_init(&list);
        for (auto &obj : storage) {
                it = ::rlc_list_it_put_back(it, obj);
        }

        /* Position at the second element (index 1). */
        it = ::rlc_list_it_init(&list);
        it = ::rlc_list_it_next(it);
        REQUIRE(storage[1] == ::rlc_list_it_node(it));

        ::rlc_list_it skipped = ::rlc_list_it_skip(it);
        ::rlc_list_it plain_next = ::rlc_list_it_next(it);

        /* skip() advances .node like next(), but keeps the original slot
         * pointer rather than the target node's own `next`. That is what
         * lets repeat() resume after the node it points at is freed by
         * something other than pop() - see process_nack_range in arq.c. */
        REQUIRE(::rlc_list_it_node(skipped) == ::rlc_list_it_node(plain_next));
        REQUIRE(skipped.slotptr == it.slotptr);
        REQUIRE(plain_next.slotptr != skipped.slotptr);

        /* Unlink node 1 by some means other than pop(), through the slot
         * pointer skip() captured. */
        *skipped.slotptr = skipped.node;

        std::vector<std::uint32_t> after_removal = {0, 2, 3};
        REQUIRE_THAT(list, matches_list(after_removal));

        it = ::rlc_list_it_repeat(skipped);
        REQUIRE(storage[2] == ::rlc_list_it_node(it));

        /* First next() call after repeat() is a no-op */
        it = ::rlc_list_it_next(it);
        REQUIRE(storage[2] == ::rlc_list_it_node(it));

        /* Subsequent calls resume normal iteration from there */
        it = ::rlc_list_it_next(it);
        REQUIRE(storage[3] == ::rlc_list_it_node(it));

        it = ::rlc_list_it_next(it);
        REQUIRE(::rlc_list_it_eoi(it));
}
