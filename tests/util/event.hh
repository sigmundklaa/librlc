
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

/* A copy of one ::rlc_event. The event and everything it points at is only
 * valid inside the callback, so it has to be copied out there. */
struct record {
        event_type type;
        std::uint32_t sn = 0;
        std::vector<std::byte> payload;
};

/* Records the events an ::rlc_context emits, in order. One per entity, so a
 * loopback test can keep the two sides apart. */
class event_handler
{
      public:
        fixture::rlc_ctx::listener_fn listener()
        {
                return [this](const ::rlc_event &ev) { capture(ev); };
        }

        /* Consume the next event, requiring it to be of the expected
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

        bool empty() const
        {
                return pos >= records.size();
        }

        /* Events recorded but not yet popped. */
        std::size_t size() const
        {
                return records.size() - pos;
        }

      private:
        void capture(const ::rlc_event &ev)
        {
                record rec{ev.type, 0, {}};
                const ::gabs_pbuf *payload = nullptr;

                switch (ev.type) {
                case ::rlc_event::RLC_EVENT_RX_DONE_DIRECT:
                        /* The payload is a union: this type carries a pbuf,
                         * not an SDU. */
                        payload = ev.buf;
                        break;

                case ::rlc_event::RLC_EVENT_RX_DONE:
                        if (ev.sdu != nullptr) {
                                rec.sn = ev.sdu->sn;
                                payload = &ev.sdu->rx.buffer.buf;
                        }
                        break;

                default:
                        if (ev.sdu != nullptr) {
                                rec.sn = ev.sdu->sn;
                        }
                        break;
                }

                /* A hand-built ::rlc_sdu has no reassembly buffer to read. */
                if (payload != nullptr && ::gabs_pbuf_okay(*payload)) {
                        rec.payload = buf::pbuf_ptr::from_weak(*payload).vec();
                }

                records.push_back(std::move(rec));
        }

        std::vector<record> records;
        std::size_t pos = 0;
};

} // namespace rlc::test::util::event

#endif /* RLC_TEST_UTIL_EVENT_HH__ */
