
#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>
#include <rlc/seg_list.h>

#include "util/mem.hh"

extern "C" {
#include "../src/tx.c"
}

namespace rlc::test
{

using namespace util;

TEST_CASE("pdu_size_adjust", "[tx][static]")
{
        static const ::rlc_config am_conf = {
                .type = RLC_AM,
                .sn_width = RLC_SN_18BIT,
        };
        static const ::rlc_config um_conf = {
                .type = RLC_UM,
                .sn_width = RLC_SN_12BIT,
        };
        static const ::rlc_config tm_conf = {
                .type = RLC_TM,
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
                ctx.conf = &um_conf;
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
                ctx.conf = &um_conf;
                ::rlc_pdu pdu = {};
                pdu.size = 5;
                pdu.flags.is_first = 1;

                REQUIRE(pdu_size_adjust(&ctx, &pdu, 5) == true);
                REQUIRE(pdu.flags.is_last == 0);
                REQUIRE(pdu.size == 3);
        }

        SECTION("TM: SDU fits, passes through unmodified")
        {
                ::rlc_context ctx = {};
                ctx.conf = &tm_conf;
                ::rlc_pdu pdu = {};
                pdu.size = 5;

                REQUIRE(pdu_size_adjust(&ctx, &pdu, 5) == true);
                REQUIRE(pdu.size == 5);
        }

        SECTION("TM: window too small must return false, not segment")
        {
                /* Spec 4.2.1.1.2: TM must not segment SDUs. A window
                 * smaller than the SDU must be refused so the caller skips
                 * the SDU rather than sending a partial one with no header. */
                ::rlc_context ctx = {};
                ctx.conf = &tm_conf;
                ::rlc_pdu pdu = {};
                pdu.size = 10;

                REQUIRE(pdu_size_adjust(&ctx, &pdu, 5) == false);
        }
}

TEST_CASE("serve_sdu", "[tx][static]")
{
        /* UM/TM mode: rlc_arq_tx_pdu_fill does not touch timers for non-AM */
        static const ::rlc_config conf = {
                .type = RLC_UM,
                .sn_width = RLC_SN_12BIT,
        };
        static const ::rlc_config tm_conf = {
                .type = RLC_TM,
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

        SECTION("TM: SDU served whole with no header overhead")
        {
                /* Spec 5.2.1.1.1: TM submits SDU to lower layer without any
                 * modification. No header bytes, no segmentation. */
                ::rlc_context ctx = {};
                ctx.conf = &tm_conf;
                ctx.alloc_misc = mem::alloc;

                ::rlc_sdu sdu = {};
                sdu.sn = 0;
                sdu.state = RLC_READY;
                ::rlc_list_init(&sdu.tx.unsent);

                ::rlc_seg seg = {.start = 0, .end = 5};
                REQUIRE(::rlc_seg_list_insert_all(&sdu.tx.unsent, seg,
                                                  mem::alloc) == 0);

                ::rlc_pdu pdu = {};
                REQUIRE(serve_sdu(&ctx, &sdu, &pdu, 5) == true);
                REQUIRE(pdu.flags.is_first == 1);
                REQUIRE(pdu.flags.is_last == 1);
                REQUIRE(pdu.size == 5);
                REQUIRE(pdu.seg_offset == 0);
                REQUIRE(sdu.state == RLC_WAIT);

                ::rlc_seg_list_clear(&sdu.tx.unsent, mem::alloc);
        }
}

}; // namespace rlc::test
