
#include <errno.h>

#include <rlc/rlc.h>

#include "util/backend.hh"
#include "util/buf.hh"
#include "util/mem.hh"
#include "util/proto.hh"

#include "gabs-overrides/timer/timer.hh"

namespace rlc::test
{

using namespace util;

static const ::rlc_config um_conf = {
        .type = RLC_UM,
        .window_size = 10,
        .sn_width = RLC_SN_12BIT,
};

TEST_CASE("tx rlc_tx queues SDU and requests backend", "[tx]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        REQUIRE(::rlc_init(&ctx, back, mem::alloc, mem::alloc) == 0);

        auto sdu_buf = buf::create(std::string("hello"));
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);
        REQUIRE(cnt == 1);
        REQUIRE(ctx.tx.next_sn == 1);

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

TEST_CASE("tx rlc_tx window full", "[tx]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        REQUIRE(::rlc_init(&ctx, back, mem::alloc, mem::alloc) == 0);

        auto sdu_buf = buf::create(std::string("hi"));
        for (int i = 0; i < 10; i++) {
                REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);
        }
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == -ENOSPC);

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

TEST_CASE("tx next_sn increments per submit", "[tx]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        REQUIRE(::rlc_init(&ctx, back, mem::alloc, mem::alloc) == 0);

        auto sdu_buf = buf::create(std::string("hi"));
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);
        REQUIRE(ctx.tx.next_sn == 1);
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);
        REQUIRE(ctx.tx.next_sn == 2);
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);
        REQUIRE(ctx.tx.next_sn == 3);

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

TEST_CASE("tx single SDU delivered whole", "[tx]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        REQUIRE(::rlc_init(&ctx, back, mem::alloc, mem::alloc) == 0);

        std::string payload = "hello";
        auto sdu_buf = buf::create(payload);
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);

        cnt = 0;
        /* AM 18-bit header = 3 bytes; payload = 5 bytes */
        auto remain = ::rlc_tx_avail(&ctx, payload.size() + 3);
        REQUIRE(remain == 0);
        REQUIRE(queue.size() == 1);

        auto [hdr, data] = proto::am::split_data(queue.front());
        REQUIRE(hdr == proto::am::header{true, proto::seginfo::ALL, 0});
        REQUIRE(data.size() == payload.size());

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

TEST_CASE("tx SDU fragmented across yields", "[tx]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        REQUIRE(::rlc_init(&ctx, back, mem::alloc, mem::alloc) == 0);

        /*
         * Payload: 6 bytes. Split into two fragments:
         *   - First yield  (max=6): 3-byte AM header + 3-byte payload (FIRST)
         *   - Second yield (max=8): 5-byte AM header w/ SO + 3-byte payload (LAST)
         */
        auto sdu_buf = buf::create(std::string("hello!"));
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);

        auto remain1 = ::rlc_tx_avail(&ctx, 6);
        REQUIRE(remain1 == 0);
        REQUIRE(queue.size() == 1);
        {
                auto [hdr, data] = proto::am::split_data(queue.front());
                REQUIRE(hdr.polled == false);
                REQUIRE(hdr.si == proto::seginfo::FIRST);
                REQUIRE(hdr.sn == 0);
                REQUIRE(data.size() == 3);
        }
        queue.pop();

        auto remain2 = ::rlc_tx_avail(&ctx, 8);
        REQUIRE(remain2 == 0);
        REQUIRE(queue.size() == 1);
        {
                auto [hdr, data] = proto::am::split_data(queue.front());
                REQUIRE(hdr.polled == true);
                REQUIRE(hdr.si == proto::seginfo::LAST);
                REQUIRE(hdr.sn == 0);
                REQUIRE(hdr.so == std::optional<std::uint16_t>(3));
                REQUIRE(data.size() == 3);
        }
        queue.pop();

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

TEST_CASE("tx multiple SDUs in single avail", "[tx]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        REQUIRE(::rlc_init(&ctx, back, mem::alloc, mem::alloc) == 0);

        auto sdu_a = buf::create(std::string("abc"));
        auto sdu_b = buf::create(std::string("xyz"));
        REQUIRE(::rlc_tx(&ctx, sdu_a, nullptr) == 0);
        REQUIRE(::rlc_tx(&ctx, sdu_b, nullptr) == 0);

        cnt = 0;
        /* Each PDU: 3-byte AM header + 3-byte payload = 6; total = 12 */
        ::rlc_tx_avail(&ctx, 20);
        REQUIRE(queue.size() == 2);

        auto [hdr0, data0] = proto::am::split_data(queue.front());
        REQUIRE(hdr0.sn == 0);
        REQUIRE(hdr0.si == proto::seginfo::ALL);
        REQUIRE(data0.size() == 3);
        queue.pop();

        auto [hdr1, data1] = proto::am::split_data(queue.front());
        REQUIRE(hdr1.sn == 1);
        REQUIRE(hdr1.si == proto::seginfo::ALL);
        REQUIRE(data1.size() == 3);
        queue.pop();

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

TEST_CASE("tx avail returns size when space is insufficient for header", "[tx]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        REQUIRE(::rlc_init(&ctx, back, mem::alloc, mem::alloc) == 0);

        auto sdu_buf = buf::create(std::string("hello"));
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);

        /* AM 18-bit header needs 3 bytes; max_size=2 is too small */
        auto remain = ::rlc_tx_avail(&ctx, 2);
        REQUIRE(remain == 2);
        REQUIRE(queue.empty());

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

TEST_CASE("tx UM single-packet omits SN", "[tx]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        REQUIRE(::rlc_init(&ctx, back, mem::alloc, mem::alloc) == 0);
        ::rlc_set_config(&ctx, &um_conf);

        std::string payload = "hello";
        auto sdu_buf = buf::create(payload);
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);

        /*
         * pdu_size_adjust optimization: max_size - 1 >= pdu_size
         * → is_last=1, header encoded as UM ALL (1 byte, SN omitted)
         */
        auto remain = ::rlc_tx_avail(&ctx, payload.size() + 1);
        REQUIRE(remain == 0);
        REQUIRE(queue.size() == 1);

        auto bytes = queue.front().vec();
        REQUIRE(bytes.size() == payload.size() + 1);

        auto it = bytes.cbegin();
        auto hdr = proto::um::header::decode(it, proto::snwidth::W12);
        REQUIRE(hdr.si == proto::seginfo::ALL);
        REQUIRE(!hdr.sn.has_value());

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

TEST_CASE("tx reset clears queue and resets sn", "[tx]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        REQUIRE(::rlc_init(&ctx, back, mem::alloc, mem::alloc) == 0);

        auto sdu_buf = buf::create(std::string("hello"));
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);
        REQUIRE(ctx.tx.next_sn == 2);

        REQUIRE(::rlc_reset(&ctx) == 0);
        REQUIRE(ctx.tx.next_sn == 0);

        /* Re-submit after reset: SN starts from 0 again */
        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);
        REQUIRE(ctx.tx.next_sn == 1);

        cnt = 0;
        /* "hello" = 5 bytes, AM 18-bit header = 3 bytes */
        ::rlc_tx_avail(&ctx, 8);
        REQUIRE(queue.size() == 1);

        auto [hdr, data] = proto::am::split_data(queue.front());
        REQUIRE(hdr.sn == 0);

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

}; // namespace rlc::test
