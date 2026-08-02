
#ifndef RLC_TEST_UTIL_FIXTURE_HH__
#define RLC_TEST_UTIL_FIXTURE_HH__

#include <functional>
#include <utility>

#include <gabs/cc/handle.hh>

#include <rlc/rlc.h>

namespace rlc::test::util::fixture
{

using ctx_trait = gabs::core::simple_handle_trait<::rlc_context>;

/*
 * Wraps an ::rlc_context using gabs's handle_wrapper, so the owning
 * instance can be recovered from the raw ctx pointer handed to
 * rlc_context's listener callback - a plain C function pointer with no
 * user-data slot. Event handling itself is forwarded to a caller-supplied
 * std::function, so the actual event storage (e.g. a local std::vector)
 * lives wherever the caller wants it, rather than inside the fixture.
 */
class rlc_ctx : public gabs::core::handle_wrapper<rlc_ctx, ctx_trait>
{
      public:
        using listener_fn = std::function<void(const ::rlc_event &)>;

        ::rlc_context *get()
        {
                return const_cast<::rlc_context *>(
                        static_cast<const ::rlc_context *>(*this));
        }

        /*
         * Sets the callback listener_trampoline forwards to, without
         * touching ctx->listener itself. Tests that hand-build an
         * ::rlc_context (bypassing rlc_init, e.g. to exercise a static
         * function directly) can assign ctx->listener =
         * rlc_ctx::listener_trampoline themselves, since rlc_attach_listener
         * needs a lock that only rlc_init initializes.
         */
        void on_event(listener_fn fn)
        {
                listener = std::move(fn);
        }

        /* Convenience for the common case of an already rlc_init'd ctx. */
        ::rlc_errno attach_listener(listener_fn fn)
        {
                on_event(std::move(fn));
                return ::rlc_attach_listener(get(),
                                             &rlc_ctx::listener_trampoline);
        }

        static void listener_trampoline(::rlc_context *raw,
                                        const ::rlc_event *ev)
        {
                auto *self = from_handle(raw);

                if (self->listener) {
                        self->listener(*ev);
                }
        }

      private:
        listener_fn listener;
};

} // namespace rlc::test::util::fixture

#endif /* RLC_TEST_UTIL_FIXTURE_HH__ */
