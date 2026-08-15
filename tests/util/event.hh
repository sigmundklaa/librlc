
#ifndef RLC_TEST_UTIL_EVENT_HH__
#define RLC_TEST_UTIL_EVENT_HH__

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>

#include "util/buf.hh"
#include "util/fixture.hh"

namespace rlc::test::util::event
{

using event_type = ::rlc_event::rlc_event_type;

inline std::string name(event_type type)
{
        switch (type) {
        case ::rlc_event::RLC_EVENT_RX_DONE:
                return "RX_DONE";
        case ::rlc_event::RLC_EVENT_RX_DONE_DIRECT:
                return "RX_DONE_DIRECT";
        case ::rlc_event::RLC_EVENT_RX_FAIL:
                return "RX_FAIL";
        case ::rlc_event::RLC_EVENT_TX_RELEASE:
                return "TX_RELEASE";
        }

        return "unknown(" + std::to_string(static_cast<int>(type)) + ")";
}

/*
 * A flattened copy of one ::rlc_event. The event itself, and everything it
 * points at, is only valid for the duration of the callback, so anything a
 * test may want to assert on has to be copied out while it runs.
 */
struct record {
        event_type type;
        std::uint32_t sn = 0;
        std::vector<std::byte> payload;
};

/*
 * Records the events an ::rlc_context emits, in order, and hands them back
 * one at a time. Storage lives in the handler rather than in the fixture, so
 * a test declares one per entity (two for a loopback pair) and keeps it in
 * local scope.
 */
class event_handler
{
      public:
        /* Pass to rlc_ctx::attach_listener, or to on_event for a context
         * that was hand-built rather than rlc_init'd. */
        fixture::rlc_ctx::listener_fn listener()
        {
                return [this](const ::rlc_event &ev) { capture(ev); };
        }

        /* Consumes the next event, requiring it to be of the expected
         * type. */
        const record &pop(event_type expected)
        {
                INFO("expected a " << name(expected) << " event");
                REQUIRE(pos < records.size());

                const record &rec = records[pos++];

                INFO("event " << (pos - 1) << " is a " << name(rec.type));
                REQUIRE(rec.type == expected);

                return rec;
        }

        /* True once every recorded event has been consumed by pop(). */
        bool empty() const
        {
                return pos >= records.size();
        }

        /* Number of events recorded but not yet consumed. */
        std::size_t size() const
        {
                return records.size() - pos;
        }

      private:
        void capture(const ::rlc_event &ev)
        {
                record rec{ev.type, 0, {}};

                switch (ev.type) {
                case ::rlc_event::RLC_EVENT_RX_DONE_DIRECT:
                        /* rlc_event's payload is a union: for this type the
                         * live member is a gabs_pbuf*, so there is no SDU to
                         * read an SN from. pbuf_ptr takes ownership and
                         * decrefs on scope exit, hence the incref. */
                        ::gabs_pbuf_incref(*ev.buf);
                        rec.payload = buf::pbuf_ptr(*ev.buf).vec();
                        break;

                case ::rlc_event::RLC_EVENT_RX_DONE:
                        if (ev.sdu != nullptr) {
                                rec.sn = ev.sdu->sn;

                                /* Tests that hand-build an ::rlc_sdu to
                                 * drive a static function directly leave
                                 * the reassembly buffer empty; there is no
                                 * payload to copy and increfing it would
                                 * dereference a null frag list. */
                                if (::gabs_pbuf_okay(ev.sdu->rx.buffer.buf)) {
                                        ::gabs_pbuf_incref(
                                                ev.sdu->rx.buffer.buf);
                                        rec.payload =
                                                buf::pbuf_ptr(
                                                        ev.sdu->rx.buffer.buf)
                                                        .vec();
                                }
                        }
                        break;

                default:
                        if (ev.sdu != nullptr) {
                                rec.sn = ev.sdu->sn;
                        }
                        break;
                }

                records.push_back(std::move(rec));
        }

        std::vector<record> records;
        std::size_t pos = 0;
};

} // namespace rlc::test::util::event

#endif /* RLC_TEST_UTIL_EVENT_HH__ */
