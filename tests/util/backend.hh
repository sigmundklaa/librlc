#ifndef RLC_TEST_UTIL_BACKEND_HH__
#define RLC_TEST_UTIL_BACKEND_HH__

#include <functional>
#include <atomic>
#include <queue>

#include <rlc/backend.h>
#include <rlc/rlc.h>

#include "util/buf.hh"

namespace rlc::test::util::backend
{

class backend
{
      public:
        using submit_fn = std::function<int(::rlc_context *, ::gabs_pbuf)>;
        using request_fn = std::function<int(::rlc_context *)>;

        backend(submit_fn submit, request_fn request)
                : submit(submit), request(request),
                  w{this, {backend::do_submit, backend::do_request}}
        {
        }

        operator ::rlc_backend *()
        {
                return &w.obj;
        }

      private:
        submit_fn submit;
        request_fn request;

        struct wrapper {
                backend *ptr;
                ::rlc_backend obj;
        } w;

        static backend *from_ctx(::rlc_context *ctx)
        {
                auto w = gabs_container_of(ctx->backend, backend::wrapper, obj);
                return w->ptr;
        }

        static int do_submit(::rlc_context *ctx, ::gabs_pbuf buf)
        {
                return from_ctx(ctx)->submit(ctx, buf);
        }

        static int do_request(::rlc_context *ctx)
        {
                return from_ctx(ctx)->request(ctx);
        }
};

inline backend::submit_fn queue_submitter(std::queue<buf::pbuf_ptr> &queue)
{
        return [&queue](::rlc_context *, ::gabs_pbuf buf) -> int {
                queue.emplace(buf);
                return 0;
        };
}

template <typename T> backend::request_fn request_counter(T &cnt)
{
        return [&cnt](::rlc_context *) -> int {
                cnt += 1;
                return 0;
        };
}

}; // namespace rlc::test::util::backend

#endif /* RLC_TEST_UTIL_BACKEND_HH__ */
