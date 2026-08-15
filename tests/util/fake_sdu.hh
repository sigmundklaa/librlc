
#ifndef RLC_TEST_UTIL_FAKE_SDU_HH__
#define RLC_TEST_UTIL_FAKE_SDU_HH__

#include <cstdint>

#include <catch2/catch_all.hpp>

#include <rlc/rlc.h>

namespace rlc::test::util
{

/* An SDU to hand to code that is driven directly, rather than reached
 * through rlc_rx_submit or rlc_tx. Holds a reference of its own, so it stays
 * readable after the code under test has released the one it was given, and
 * frees only that reference. */
class fake_sdu
{
      public:
        fake_sdu(::rlc_context *ctx, std::uint32_t sn,
                 enum rlc_sdu_state state, bool is_tx = false)
                : sdu(::rlc_sdu_alloc(ctx, is_tx))
        {
                REQUIRE(sdu != nullptr);

                sdu->sn = sn;
                sdu->state = state;
        }

        fake_sdu(const fake_sdu &) = delete;

        ~fake_sdu()
        {
                ::rlc_sdu_decref(sdu);
        }

        ::rlc_sdu *get() const
        {
                return sdu;
        }

        /* Reference for something that takes ownership, such as a queue. */
        ::rlc_sdu *strong() const
        {
                ::rlc_sdu_incref(sdu);

                return sdu;
        }

      private:
        ::rlc_sdu *sdu;
};

} // namespace rlc::test::util

#endif /* RLC_TEST_UTIL_FAKE_SDU_HH__ */
