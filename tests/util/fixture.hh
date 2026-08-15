
#ifndef RLC_TEST_UTIL_FIXTURE_HH__
#define RLC_TEST_UTIL_FIXTURE_HH__

#include <functional>
#include <utility>

#include <gabs/cc/handle.hh>

#include <rlc/rlc.h>

namespace rlc::test::util::fixture
{

using ctx_trait = gabs::core::simple_handle_trait<::rlc_context>;

class rlc_ctx : public gabs::core::handle_wrapper<rlc_ctx, ctx_trait>
{
      public:
        using listener_fn = std::function<void(const ::rlc_event &)>;

        ::rlc_context *get()
        {
                return const_cast<::rlc_context *>(
                        static_cast<const ::rlc_context *>(*this));
        }

        /* Set the callback without touching ctx->listener, for a context
         * built without rlc_init - rlc_attach_listener needs a lock only
         * rlc_init sets up. */
        void on_event(listener_fn fn)
        {
                listener = std::move(fn);
        }

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
