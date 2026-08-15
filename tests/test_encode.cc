
#include <cerrno>

#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>

#include "encode.h"

#include "util/buf.hh"
#include "util/mem.hh"
#include "util/proto.hh"

namespace rlc::test
{

using namespace util;

TEST_CASE("am pdu header encode/decode", "[encode][am]")
{
        auto sn_width = GENERATE(RLC_SN_12BIT, RLC_SN_18BIT);
        auto si = GENERATE(proto::seginfo::ALL, proto::seginfo::FIRST,
                           proto::seginfo::LAST, proto::seginfo::NEITHER);
        auto polled = GENERATE(false, true);

        auto w = (sn_width == RLC_SN_12BIT) ? proto::snwidth::W12
                                            : proto::snwidth::W18;

        std::uint32_t sn = (w == proto::snwidth::W12) ? 0xfff : 0x3ffff;
        std::uint16_t so = 0x1234;
        bool has_so = (si == proto::seginfo::LAST ||
                      si == proto::seginfo::NEITHER);

        ::rlc_pdu pdu = {};
        pdu.flags.is_first =
                (si == proto::seginfo::ALL || si == proto::seginfo::FIRST);
        pdu.flags.is_last =
                (si == proto::seginfo::ALL || si == proto::seginfo::LAST);
        pdu.flags.polled = polled;
        pdu.sn = sn;
        if (has_so) {
                pdu.seg_offset = so;
        }

        proto::am::header expect_hdr{
                polled, si, sn,
                has_so ? std::optional<std::uint16_t>(so) : std::nullopt};
        auto expect_bytes = expect_hdr.encode(w);

        auto out = buf::create(RLC_PDU_HEADER_MAX_SIZE);
        ::rlc_pdu_encode(&pdu, out, RLC_AM, sn_width);
        REQUIRE_THAT(out, buf::matches_contents(expect_bytes));

        REQUIRE(::rlc_pdu_header_size(&pdu, RLC_AM, sn_width) ==
               expect_bytes.size());

        std::vector<std::byte> payload = {std::byte{0xaa}, std::byte{0xbb}};
        auto in_bytes = expect_bytes;
        in_bytes.insert(in_bytes.end(), payload.begin(), payload.end());
        auto in = buf::create(in_bytes);

        ::rlc_pdu pdu2 = {};
        auto status = ::rlc_pdu_decode(&pdu2, in, RLC_AM, sn_width);
        REQUIRE(status == 0);
        REQUIRE(pdu2.flags.is_first == pdu.flags.is_first);
        REQUIRE(pdu2.flags.is_last == pdu.flags.is_last);
        REQUIRE(pdu2.flags.polled == pdu.flags.polled);
        REQUIRE(pdu2.sn == pdu.sn);
        if (has_so) {
                REQUIRE(pdu2.seg_offset == pdu.seg_offset);
        }
        REQUIRE_THAT(in, buf::matches_contents(payload));
}

TEST_CASE("um pdu header encode/decode", "[encode][um]")
{
        auto sn_width = GENERATE(RLC_SN_6BIT, RLC_SN_12BIT);
        auto si = GENERATE(proto::seginfo::ALL, proto::seginfo::FIRST,
                           proto::seginfo::LAST, proto::seginfo::NEITHER);

        auto w = (sn_width == RLC_SN_6BIT) ? proto::snwidth::W6
                                           : proto::snwidth::W12;

        bool is_all = (si == proto::seginfo::ALL);
        bool has_sn = !is_all;
        bool has_so = (si == proto::seginfo::LAST ||
                      si == proto::seginfo::NEITHER);

        std::uint32_t sn = (w == proto::snwidth::W6) ? 0x3f : 0xfff;
        std::uint16_t so = 0x1234;

        ::rlc_pdu pdu = {};
        pdu.flags.is_first =
                (si == proto::seginfo::ALL || si == proto::seginfo::FIRST);
        pdu.flags.is_last =
                (si == proto::seginfo::ALL || si == proto::seginfo::LAST);
        if (has_sn) {
                pdu.sn = sn;
        }
        if (has_so) {
                pdu.seg_offset = so;
        }

        proto::um::header expect_hdr{
                si, has_sn ? std::optional<std::uint32_t>(sn) : std::nullopt,
                has_so ? std::optional<std::uint16_t>(so) : std::nullopt};
        auto expect_bytes = expect_hdr.encode(w);

        auto out = buf::create(RLC_PDU_HEADER_MAX_SIZE);
        ::rlc_pdu_encode(&pdu, out, RLC_UM, sn_width);
        REQUIRE_THAT(out, buf::matches_contents(expect_bytes));

        REQUIRE(::rlc_pdu_header_size(&pdu, RLC_UM, sn_width) ==
               expect_bytes.size());

        std::vector<std::byte> payload = {std::byte{0xaa}, std::byte{0xbb}};
        auto in_bytes = expect_bytes;
        in_bytes.insert(in_bytes.end(), payload.begin(), payload.end());
        auto in = buf::create(in_bytes);

        ::rlc_pdu pdu2 = {};
        auto status = ::rlc_pdu_decode(&pdu2, in, RLC_UM, sn_width);
        REQUIRE(status == 0);
        REQUIRE(pdu2.flags.is_first == pdu.flags.is_first);
        REQUIRE(pdu2.flags.is_last == pdu.flags.is_last);
        if (has_sn) {
                REQUIRE(pdu2.sn == pdu.sn);
        }
        if (has_so) {
                REQUIRE(pdu2.seg_offset == pdu.seg_offset);
        }
        REQUIRE_THAT(in, buf::matches_contents(payload));
}

TEST_CASE("tm pdu header encode/decode is a no-op", "[encode][tm]")
{
        ::rlc_pdu pdu = {};
        pdu.flags.is_first = true;
        pdu.flags.is_last = true;

        auto out = buf::create(RLC_PDU_HEADER_MAX_SIZE);
        ::rlc_pdu_encode(&pdu, out, RLC_TM, RLC_SN_12BIT);
        REQUIRE(::gabs_pbuf_size(out) == 0);

        REQUIRE(::rlc_pdu_header_size(&pdu, RLC_TM, RLC_SN_12BIT) == 0);

        std::vector<std::byte> payload = {std::byte{0xaa}, std::byte{0xbb}};
        auto in = buf::create(payload);

        ::rlc_pdu pdu2 = {};
        auto status = ::rlc_pdu_decode(&pdu2, in, RLC_TM, RLC_SN_12BIT);
        REQUIRE(status == 0);
        REQUIRE_THAT(in, buf::matches_contents(payload));
}

TEST_CASE("am status pdu ack header encode/decode", "[encode][am][status]")
{
        auto sn_width = GENERATE(RLC_SN_12BIT, RLC_SN_18BIT);
        auto ext = GENERATE(false, true);

        auto w = (sn_width == RLC_SN_12BIT) ? proto::snwidth::W12
                                            : proto::snwidth::W18;

        std::uint32_t sn = (w == proto::snwidth::W12) ? 0xfff : 0x3ffff;

        ::rlc_pdu pdu = {};
        pdu.flags.is_status = true;
        pdu.sn = sn;
        pdu.flags.ext = ext;

        /* rlc_pdu_encode only writes the ACK_SN/E1 part; the NACK list is
         * rlc_status_encode's job. A proto::am::status with no parts is
         * that same layout, `has_parts` sitting where E1 does. */
        proto::am::status expect{sn, {}};
        auto expect_bytes = expect.encode(w);
        if (ext) {
                expect_bytes[2] |= (w == proto::snwidth::W12)
                                           ? std::byte{0x80}
                                           : std::byte{0x02};
        }

        auto out = buf::create(RLC_STATUS_MAX_SIZE);
        ::rlc_pdu_encode(&pdu, out, RLC_AM, sn_width);
        REQUIRE_THAT(out, buf::matches_contents(expect_bytes));

        auto in = buf::create(expect_bytes);

        ::rlc_pdu pdu2 = {};
        auto status = ::rlc_pdu_decode(&pdu2, in, RLC_AM, sn_width);
        REQUIRE(status == 0);
        REQUIRE(pdu2.flags.is_status == true);
        REQUIRE(pdu2.sn == pdu.sn);
        REQUIRE(pdu2.flags.ext == pdu.flags.ext);
}

TEST_CASE("am status pdu with reserved CPT is rejected",
         "[encode][am][status]")
{
        auto sn_width = GENERATE(RLC_SN_12BIT, RLC_SN_18BIT);
        auto cpt = GENERATE(0b001, 0b010, 0b011, 0b100, 0b101, 0b110, 0b111);

        auto w = (sn_width == RLC_SN_12BIT) ? proto::snwidth::W12
                                            : proto::snwidth::W18;

        proto::am::status expect{0, {}};
        auto bytes = expect.encode(w);
        bytes[0] |= static_cast<std::byte>(cpt << 4);

        auto in = buf::create(bytes);

        ::rlc_pdu pdu = {};
        REQUIRE(::rlc_pdu_decode(&pdu, in, RLC_AM, sn_width) == -ENOTSUP);

        /* Rejected PDU must be left intact for the caller to discard. */
        REQUIRE_THAT(in, buf::matches_contents(bytes));
}

TEST_CASE("am status nack part encode/decode", "[encode][am][status]")
{
        auto sn_width = GENERATE(RLC_SN_12BIT, RLC_SN_18BIT);
        auto has_offset = GENERATE(false, true);
        auto has_range = GENERATE(false, true);
        auto more = GENERATE(false, true);

        auto w = (sn_width == RLC_SN_12BIT) ? proto::snwidth::W12
                                            : proto::snwidth::W18;

        std::uint32_t sn = (w == proto::snwidth::W12) ? 0xfff : 0x3ffff;

        ::rlc_pdu_status status = {};
        status.nack_sn = sn;
        status.ext.has_more = more;
        status.ext.has_offset = has_offset;
        status.ext.has_range = has_range;

        proto::am::status_part expect{sn, std::nullopt, std::nullopt,
                                      std::nullopt};

        if (has_offset) {
                status.offset.start = 0x1111;
                status.offset.end = 0x2222;
                expect.sostart = 0x1111;
                expect.soend = 0x2222;
        }
        if (has_range) {
                status.range = 0xab;
                expect.range = 0xab;
        }

        auto expect_bytes = expect.encode(more, w);

        auto out = buf::create(RLC_STATUS_MAX_SIZE);
        ::rlc_status_encode(&status, out, sn_width);
        REQUIRE_THAT(out, buf::matches_contents(expect_bytes));

        REQUIRE(::rlc_status_size(&status, sn_width) == expect_bytes.size());

        std::vector<std::byte> payload = {std::byte{0xee}};
        auto in_bytes = expect_bytes;
        in_bytes.insert(in_bytes.end(), payload.begin(), payload.end());
        auto in = buf::create(in_bytes);

        ::rlc_pdu_status status2 = {};
        auto err = ::rlc_status_decode(&status2, in, sn_width);
        REQUIRE(err == 0);
        REQUIRE(status2.nack_sn == status.nack_sn);
        REQUIRE(status2.ext.has_more == status.ext.has_more);
        REQUIRE(status2.ext.has_offset == status.ext.has_offset);
        REQUIRE(status2.ext.has_range == status.ext.has_range);
        if (has_offset) {
                REQUIRE(status2.offset.start == status.offset.start);
                REQUIRE(status2.offset.end == status.offset.end);
        }
        if (has_range) {
                REQUIRE(status2.range == status.range);
        }
        REQUIRE_THAT(in, buf::matches_contents(payload));
}

}; // namespace rlc::test
