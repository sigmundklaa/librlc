
#include <errno.h>

#include <rlc/rlc.h>
#include <rlc/seg_list.h>

#include "util/backend.hh"
#include "util/buf.hh"
#include "util/mem.hh"
#include "util/proto.hh"

#include "gabs-overrides/timer/timer.hh"

extern "C" {
#include "../src/tx.c"
}

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

        /* 6-byte payload split across two yields: FIRST (3 bytes) then LAST (3 bytes) */
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

        REQUIRE(::rlc_tx(&ctx, sdu_buf, nullptr) == 0);
        REQUIRE(ctx.tx.next_sn == 1);

        cnt = 0;
        ::rlc_tx_avail(&ctx, 8);
        REQUIRE(queue.size() == 1);

        auto [hdr, data] = proto::am::split_data(queue.front());
        REQUIRE(hdr.sn == 0);

        REQUIRE(::rlc_deinit(&ctx) == 0);
}

TEST_CASE("pdu_size_adjust", "[tx][static]")
{
        static const ::rlc_config am_conf = {
                .type = RLC_AM,
                .sn_width = RLC_SN_18BIT,
        };
        static const ::rlc_config um_conf_12 = {
                .type = RLC_UM,
                .sn_width = RLC_SN_12BIT,
        };

        SECTION("AM: PDU fits without trimming")
        {
                ::rlc_context ctx = {};
                ctx.conf = &am_conf;
                ::rlc_pdu pdu = {};
                pdu.size = 5;
                pdu.flags.is_first = 1;

                REQUIRE(pdu_size_adjust(&ctx, &pdu, 8) == true);
                REQUIRE(pdu.size == 5);
        }

        SECTION("AM: PDU trimmed to fit")
        {
                ::rlc_context ctx = {};
                ctx.conf = &am_conf;
                ::rlc_pdu pdu = {};
                pdu.size = 10;
                pdu.flags.is_first = 1;

                REQUIRE(pdu_size_adjust(&ctx, &pdu, 8) == true);
                REQUIRE(pdu.size == 5);
        }

        SECTION("AM: max_size smaller than header returns false")
        {
                ::rlc_context ctx = {};
                ctx.conf = &am_conf;
                ::rlc_pdu pdu = {};
                pdu.size = 5;
                pdu.flags.is_first = 1;

                REQUIRE(pdu_size_adjust(&ctx, &pdu, 2) == false);
        }

        SECTION("AM: non-first PDU includes SO in header size")
        {
                ::rlc_context ctx = {};
                ctx.conf = &am_conf;
                ::rlc_pdu pdu = {};
                pdu.size = 5;
                pdu.flags.is_first = 0;

                /* header = 3 (SN) + 2 (SO) = 5; trimmed from 5 to 3 */
                REQUIRE(pdu_size_adjust(&ctx, &pdu, 8) == true);
                REQUIRE(pdu.size == 3);
        }

        SECTION("UM: single-packet optimization sets is_last and returns early")
        {
                ::rlc_context ctx = {};
                ctx.conf = &um_conf_12;
                ::rlc_pdu pdu = {};
                pdu.size = 5;
                pdu.flags.is_first = 1;

                REQUIRE(pdu_size_adjust(&ctx, &pdu, 6) == true);
                REQUIRE(pdu.flags.is_last == 1);
                REQUIRE(pdu.size == 5);
        }

        SECTION("UM: optimization skipped when max_size - 1 < pdu_size")
        {
                ::rlc_context ctx = {};
                ctx.conf = &um_conf_12;
                ::rlc_pdu pdu = {};
                pdu.size = 5;
                pdu.flags.is_first = 1;

                REQUIRE(pdu_size_adjust(&ctx, &pdu, 5) == true);
                REQUIRE(pdu.flags.is_last == 0);
                REQUIRE(pdu.size == 3);
        }
}

TEST_CASE("serve_sdu", "[tx][static]")
{
        /* UM mode: rlc_arq_tx_pdu_fill does not touch timers */
        static const ::rlc_config conf = {
                .type = RLC_UM,
                .sn_width = RLC_SN_12BIT,
        };

        SECTION("serves complete SDU in one shot")
        {
                ::rlc_context ctx = {};
                ctx.conf = &conf;
                ctx.alloc_misc = mem::alloc;

                ::rlc_sdu sdu = {};
                sdu.sn = 7;
                sdu.state = RLC_READY;
                ::rlc_list_init(&sdu.tx.unsent);

                ::rlc_seg seg = {.start = 0, .end = 5};
                REQUIRE(::rlc_seg_list_insert_all(&sdu.tx.unsent, seg,
                                                  mem::alloc) == 0);

                ::rlc_pdu pdu = {};
                REQUIRE(serve_sdu(&ctx, &sdu, &pdu, 6) == true);
                REQUIRE(pdu.sn == 7);
                REQUIRE(pdu.flags.is_first == 1);
                REQUIRE(pdu.flags.is_last == 1);
                REQUIRE(pdu.size == 5);
                REQUIRE(pdu.seg_offset == 0);
                REQUIRE(sdu.state == RLC_WAIT);

                ::rlc_seg_list_clear(&sdu.tx.unsent, mem::alloc);
        }

        SECTION("trims PDU and advances segment start")
        {
                ::rlc_context ctx = {};
                ctx.conf = &conf;
                ctx.alloc_misc = mem::alloc;

                ::rlc_sdu sdu = {};
                sdu.sn = 3;
                sdu.state = RLC_READY;
                ::rlc_list_init(&sdu.tx.unsent);

                ::rlc_seg seg = {.start = 0, .end = 6};
                REQUIRE(::rlc_seg_list_insert_all(&sdu.tx.unsent, seg,
                                                  mem::alloc) == 0);

                ::rlc_pdu pdu = {};
                REQUIRE(serve_sdu(&ctx, &sdu, &pdu, 5) == true);
                REQUIRE(pdu.flags.is_first == 1);
                REQUIRE(pdu.flags.is_last == 0);
                REQUIRE(pdu.size == 3);
                REQUIRE(pdu.seg_offset == 0);
                REQUIRE(sdu.state == RLC_READY);

                ::rlc_seg_list_clear(&sdu.tx.unsent, mem::alloc);
        }
}

}; // namespace rlc::test
