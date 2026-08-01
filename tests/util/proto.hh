
#ifndef RLC_TEST_UTIL_PROTO_HH__
#define RLC_TEST_UTIL_PROTO_HH__

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rlc::test::util::proto
{

enum seginfo {
        ALL = 0b00,
        FIRST = 0b01,
        LAST = 0b10,
        NEITHER = 0b11,
};

enum snwidth {
        W6,
        W12,
        W18,
};

namespace um
{
struct header {
        seginfo si = seginfo::ALL;
        std::optional<std::uint32_t> sn;
        std::optional<std::uint16_t> so;

        bool operator==(const header &) const = default;

        std::vector<std::byte> encode(snwidth w = snwidth::W12) const
        {
                std::vector<std::byte> ret;

                assert(w != snwidth::W18);

                std::uint8_t b = si << 6;

                if (si == seginfo::ALL) {
                        ret.push_back(static_cast<std::byte>(b));
                        return ret;
                }

                assert(sn.has_value());

                if (w == snwidth::W6) {
                        auto val = sn.value() & 0x3f;

                        assert(val == sn.value());
                        b |= val;

                        ret.push_back(static_cast<std::byte>(b));
                } else {
                        auto val = sn.value() & 0xfff;

                        assert(val == sn.value());
                        b |= val >> 8;

                        ret.push_back(static_cast<std::byte>(b));
                        ret.push_back(static_cast<std::byte>(val & 0xff));
                }

                if (si == seginfo::FIRST) {
                        return ret;
                }

                assert(so.has_value());
                auto val = so.value() & 0xffff;
                assert(val == so.value());

                ret.push_back(static_cast<std::byte>(val >> 8));
                ret.push_back(static_cast<std::byte>(val & 0xff));

                return ret;
        }

        static header decode(std::vector<std::byte>::const_iterator &it,
                             snwidth w = snwidth::W12)
        {
                header h;

                assert(w != snwidth::W18);

                std::uint8_t b = static_cast<std::uint8_t>(*it++);
                h.si = seginfo(b >> 6);

                if (h.si == seginfo::ALL) {
                        return h;
                }

                if (w == snwidth::W6) {
                        h.sn = b & 0x3f;
                } else {
                        h.sn = (b & 0xf) << 8;
                        b = static_cast<std::uint8_t>(*it++);
                        h.sn.value() |= b;
                }

                if (h.si == seginfo::FIRST) {
                        return h;
                }

                h.so = static_cast<std::uint8_t>(*it++) << 8;
                h.so.value() |= static_cast<std::uint8_t>(*it++);

                return h;
        }
};
}; // namespace um

namespace am
{

struct header {
        bool polled = false;
        seginfo si = seginfo::ALL;
        std::uint32_t sn;
        std::optional<std::uint16_t> so;

        bool operator==(const header &) const = default;

        std::vector<std::byte> encode(snwidth w = snwidth::W18)
        {
                std::vector<std::byte> ret;

                assert(w != snwidth::W6);

                std::uint8_t b = (1 << 7) | (polled << 6) | (si << 4);

                if (w == snwidth::W12) {
                        auto val = sn & 0xfff;
                        assert(val == sn);

                        b |= val >> 8;
                        ret.push_back(static_cast<std::byte>(b));
                        ret.push_back(static_cast<std::byte>(val & 0xff));
                } else {
                        auto val = sn & 0x3ffff;
                        assert(val == sn);

                        b |= val >> 16;
                        ret.push_back(static_cast<std::byte>(b));
                        ret.push_back(
                                static_cast<std::byte>((val >> 8) & 0xff));
                        ret.push_back(static_cast<std::byte>(val & 0xff));
                }

                if (si == seginfo::FIRST || si == seginfo::ALL) {
                        return ret;
                }

                auto val = so.value() & 0xffff;
                assert(val == so.value());

                ret.push_back(static_cast<std::byte>(val >> 8));
                ret.push_back(static_cast<std::byte>(val & 0xff));

                return ret;
        }

        static header decode(std::vector<std::byte>::const_iterator &it,
                             snwidth w = snwidth::W18)
        {
                header h;

                assert(w != snwidth::W6);

                std::uint8_t b = static_cast<std::uint8_t>(*it++);
                assert((b >> 7) == 1 /* D/C bit */);

                h.polled = (b >> 6) & 0x1;
                h.si = seginfo((b >> 4) & 0x3);

                if (w == snwidth::W12) {
                        h.sn = (b & 0xf) << 8;
                        h.sn |= static_cast<std::uint8_t>(*it++);
                } else {
                        h.sn = (b & 0x3) << 16;
                        h.sn |= static_cast<std::uint8_t>(*it++) << 8;
                        h.sn |= static_cast<std::uint8_t>(*it++);
                }

                if (h.si == seginfo::ALL || h.si == seginfo::FIRST) {
                        return h;
                }

                h.so = 0;
                h.so.value() |= static_cast<std::uint8_t>(*it++) << 8;
                h.so.value() |= static_cast<std::uint8_t>(*it++);

                return h;
        }
};

struct status_part {
        std::uint32_t sn;
        std::optional<std::uint16_t> sostart;
        std::optional<std::uint16_t> soend;
        std::optional<std::uint8_t> range;

        bool operator==(const status_part &) const = default;

        std::vector<std::byte> encode(bool more, snwidth w = snwidth::W18)
        {
                std::vector<std::byte> ret;
                unsigned int flags;

                assert(w != snwidth::W6);
                assert(sostart.has_value() == soend.has_value());

                flags = (more << 2) | (sostart.has_value() << 1) |
                        (range.has_value() << 0);

                if (w == snwidth::W12) {
                        flags <<= 1;
                        ret.push_back(static_cast<std::byte>((sn >> 4) & 0xff));
                        ret.push_back(static_cast<std::byte>(
                                ((sn & 0xff) << 4) | flags));
                } else {
                        flags <<= 3;
                        ret.push_back(
                                static_cast<std::byte>((sn >> 10) & 0xff));
                        ret.push_back(static_cast<std::byte>((sn >> 2) & 0xff));
                        ret.push_back(static_cast<std::byte>(((sn & 0x3) << 6) |
                                                             flags));
                }

                if (sostart.has_value()) {
                        ret.push_back(
                                static_cast<std::byte>(sostart.value() >> 8));
                        ret.push_back(
                                static_cast<std::byte>(sostart.value() & 0xff));
                        ret.push_back(
                                static_cast<std::byte>(soend.value() >> 8));
                        ret.push_back(
                                static_cast<std::byte>(soend.value() & 0xff));
                }

                if (range.has_value()) {
                        ret.push_back(static_cast<std::byte>(range.value()));
                }

                return ret;
        }

        static std::pair<status_part, bool>
        decode(std::vector<std::byte>::const_iterator &it,
               snwidth w = snwidth::W18)
        {
                status_part ret;
                bool more = false;

                assert(w != snwidth::W6);

                ret.sn = static_cast<std::uint32_t>(*it++);
                std::uint8_t flags;

                if (w == snwidth::W12) {
                        auto b = static_cast<std::uint8_t>(*it++);

                        ret.sn = (ret.sn << 4) | ((b >> 4) & 0xf);
                        flags = (b >> 1) & 0x7;
                } else {
                        ret.sn = (ret.sn << 8) |
                                 (static_cast<std::uint8_t>(*it++));

                        auto b = static_cast<std::uint8_t>(*it++);

                        ret.sn = (ret.sn << 2) | ((b >> 6) & 0x3);
                        flags = (b >> 3) & 0x7;
                }

                if ((flags >> 2) & 0x1) {
                        more = true;
                }

                if ((flags >> 1) & 0x1) {
                        ret.sostart = static_cast<std::uint8_t>(*it++) << 8;
                        ret.sostart.value() |= static_cast<std::uint8_t>(*it++);
                        ret.soend = static_cast<std::uint8_t>(*it++) << 8;
                        ret.soend.value() |= static_cast<std::uint8_t>(*it++);
                }

                if ((flags >> 0) & 0x1) {
                        ret.range = static_cast<std::uint8_t>(*it++);
                }

                return {ret, more};
        }
};

struct status {
        std::uint32_t sn;
        std::vector<status_part> parts;

        bool operator==(const status &) const = default;

        std::vector<std::byte> encode(snwidth w = snwidth::W18)
        {
                std::vector<std::byte> ret;
                bool has_parts = parts.size() > 0;

                assert(w != snwidth::W6);

                if (w == snwidth::W12) {
                        ret.push_back(static_cast<std::byte>((sn >> 8) & 0xf));
                        ret.push_back(static_cast<std::byte>(sn & 0xff));
                        ret.push_back(static_cast<std::byte>(has_parts << 7));
                } else {
                        ret.push_back(static_cast<std::byte>((sn >> 14) & 0xf));
                        ret.push_back(static_cast<std::byte>((sn >> 6) & 0xff));
                        ret.push_back(static_cast<std::byte>(
                                ((sn & 0x3f) << 2) | (has_parts << 1)));
                }

                for (auto it = parts.begin(); it < parts.end(); it++) {
                        auto bytes = it->encode(it != parts.end(), w);
                        ret.insert(ret.end(), bytes.begin(), bytes.end());
                }

                return ret;
        }

        static status decode(std::vector<std::byte>::const_iterator it,
                             snwidth w = snwidth::W18)
        {
                assert(w != snwidth::W6);
                status ret;
                bool more;

                if (w == snwidth::W12) {
                        ret.sn = (static_cast<std::uint32_t>(*it++) & 0xf) << 8;
                        ret.sn |= static_cast<std::uint32_t>(*it++) & 0xff;
                        more = (static_cast<std::uint8_t>(*it++) >> 7) & 0x1;
                } else {
                        ret.sn = (static_cast<std::uint32_t>(*it++) & 0xf)
                                 << 14;
                        ret.sn |= (static_cast<std::uint32_t>(*it++) & 0xff)
                                  << 6;

                        auto b = static_cast<std::uint8_t>(*it++);
                        ret.sn |= (b >> 2) & 0x3f;
                        more = (b >> 1) & 0x1;
                }

                while (more) {
                        auto [part, part_more] = status_part::decode(it, w);
                        ret.parts.push_back(part);

                        more = part_more;
                }

                return ret;
        }
};

inline std::pair<header, std::vector<std::byte>>
split_data(const std::vector<std::byte> &data, snwidth w = snwidth::W18)
{
        auto it = data.begin();
        auto h = header::decode(it, w);

        return {h, std::vector<std::byte>(it, data.end())};
}

}; // namespace am

}; // namespace rlc::test::util::proto

#endif /* RLC_TEST_UTIL_PROTO_HH__ */
