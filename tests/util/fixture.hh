
#ifndef RLC_TEST_UTIL_FIXTURE_HH__
#define RLC_TEST_UTIL_FIXTURE_HH__

#include <functional>
#include <utility>

#include <gabs/cc/handle.hh>

#include <rlc/rlc.h>

namespace rlc::test::util::fixture
{

/*
 * Equivalent to gabs::core::simple_handle_trait<::rlc_context>, but with a
 * working from(): as of gabs commit e1d75b9, container_of(const T *,
 * T Container::*)'s body computes the member offset via
 * `&nullcont->*member` - but unary & binds tighter than ->*, so this
 * parses as `(&nullcont)->*member` (LHS type Container**, which ->*
 * then implicitly dereferences once via its own `(*a).*b` semantics,
 * landing on non-class type Container* - not the intended
 * `&(nullcont->*member)`). Confirmed via a minimal repro independent of
 * rlc_context; the parenthesized form compiles and works correctly on
 * its own, so this looks like the last remaining issue once the const
 * placement (see git history of this file) was fixed. Sidesteps it via
 * the same gabs_container_of macro dynamic.hh's dyn_alloc trait already
 * uses.
 */
class ctx_trait
{
      public:
        using handle_type = ::rlc_context;

        const handle_type *to() const
        {
                return &handle_;
        }

        static ctx_trait *from(const handle_type *h)
        {
                return gabs_container_of(h, ctx_trait, handle_);
        }

      protected:
        handle_type handle_;
};

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
