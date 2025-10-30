
#ifndef RLC_CXX_H__
#define RLC_CXX_H__

#include <rlc/rlc.h>
#include <rlc/buf.h>

#ifdef __cplusplus

#include <memory>
#include <algorithm>
#include <functional>
#include <cstdint>
#include <cerrno>
#include <stdexcept>
#include <cstring>

namespace rlc
{

class error : public std::runtime_error
{
      public:
        using std::runtime_error::runtime_error;
};

using config = ::rlc_config;
using sdu_type = ::rlc_sdu_type;
using alloc_type = ::rlc_alloc_type;
using event = ::rlc_event;

template <class Context> class backend;

namespace detail
{

template <class Context> inline Context &get_ctx(::rlc_context *ctx);

template <class Context> struct methods {
        static ::rlc_errno tx_submit_cb(::rlc_context *ctx, ::rlc_buf *buf);
        static ::rlc_errno tx_request_cb(::rlc_context *ctx);
        static void event_cb(::rlc_context *ctx, const ::rlc_event *event);
        static void *alloc_cb(::rlc_context *ctx, std::size_t size,
                              alloc_type type);
        static void dealloc_cb(::rlc_context *ctx, void *memory,
                               alloc_type type);

        operator const ::rlc_methods *() const
        {
                static const ::rlc_methods native = {
                        tx_submit_cb, tx_request_cb, event_cb,
                        alloc_cb,     dealloc_cb,
                };
                return &native;
        }
};

}; // namespace detail

class vector_buffer
{
      public:
        vector_buffer(const std::shared_ptr<std::vector<std::byte>> &data)
                : data(data)
        {
        }

        vector_buffer(const ::rlc_buf *native)
                : data(std::make_shared<std::vector<std::byte>>())
        {
                const ::rlc_buf *cur;

                for (rlc_each_node(native, cur, next)) {
                        std::copy_n(
                                reinterpret_cast<const std::byte *>(cur->data),
                                cur->size, std::back_inserter(*data));
                }
        }

        template <class Context>::rlc_buf *native(Context &ctx) const
        {
                ::rlc_buf *buf;

                buf = ::rlc_buf_alloc(&ctx.native(), data->size());
                if (buf == NULL) {
                        throw std::bad_alloc();
                }

                ::rlc_buf_put(buf, reinterpret_cast<const void *>(data->data()),
                              data->size());

                return buf;
        }

        operator std::vector<std::byte> &()
        {
                return *data;
        }

        operator const std::vector<std::byte> &() const
        {
                return *data;
        }

        operator const std::shared_ptr<std::vector<std::byte>> &() const
        {
                return data;
        }

        operator std::shared_ptr<std::vector<std::byte>> &()
        {
                return data;
        }

      private:
        std::shared_ptr<std::vector<std::byte>> data;
};

template <class Buffer = vector_buffer,
          template <class> class Allocator = std::allocator>
class context
{
      public:
        using buffer_type = Buffer;
        using allocator_type = Allocator<std::byte>;

        context(sdu_type type, std::function<void(const event &)> eh,
                const std::shared_ptr<backend<context>> &back,
                const std::shared_ptr<config> &conf,
                const allocator_type &alloc = allocator_type())
                : allocator_(alloc), event_handler_(eh), backend_(back)
        {
                ::rlc_errno status;
                detail::methods<context> methods;

                status = ::rlc_init(&ctx_, type, conf.get(),
                                    static_cast<const ::rlc_methods *>(methods),
                                    this);
                if (status != 0) {
                        throw error(
                                std::string("context(): init failed; status=") +
                                std::strerror(status));
                }
        }

        ~context()
        {
                (void)::rlc_deinit(&ctx_);
        }

        ::rlc_sdu *send(const buffer_type &buf)
        {
                auto native = buf.native(*this);
                auto ret = ::rlc_send(&ctx_, native, nullptr);
                if (ret != 0) {
                        throw error(std::string("send(): failed; status=") +
                                    std::strerror(ret));
                }

                ::rlc_buf_decref(native, &ctx_);

                return nullptr;
        }

        std::size_t tx_avail(std::size_t size)
        {
                return ::rlc_tx_avail(&ctx_, size);
        }

        void rx_submit(buffer_type &buf)
        {
                auto native = ::rlc_rx_submit(&ctx_, buf.native(*this));
        }

        void rx_submit(const buffer_type &buf)
        {
                auto native = ::rlc_rx_submit(&ctx_, buf.native(*this));
        }

        ::rlc_context &native()
        {
                return ctx_;
        }

      private:
        ::rlc_context ctx_;
        allocator_type allocator_;

        std::function<void(const event &)> event_handler_;

        std::shared_ptr<backend<context>> backend_;

        template <class Context>
        friend inline Context &detail::get_ctx(::rlc_context *ctx);

        friend detail::methods<context>;
};

template <class Context> struct backend {
        virtual ~backend() = default;
        virtual void submit(const Context &,
                            const typename Context::buffer_type &) const = 0;
        virtual void request(const Context &) const = 0;
};

namespace detail
{

template <class T, class U, U T::*Member> inline T &container(U &member)
{
        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(&member);
        std::uintptr_t offset =
                reinterpret_cast<std::uintptr_t>(&(((T *)(nullptr))->*Member));

        return *reinterpret_cast<T *>(addr - offset);
}

template <class Context> inline Context &get_ctx(::rlc_context *ctx)
{
        return container<Context, ::rlc_context, &Context::ctx_>(*ctx);
}

template <class Context>
::rlc_errno methods<Context>::tx_submit_cb(::rlc_context *ctx_arg,
                                           ::rlc_buf *buf)
{
        auto &ctx = get_ctx<Context>(ctx_arg);
        try {
                using buffer_type = typename Context::buffer_type;
                auto bufcpy = buffer_type(buf);

                ::rlc_buf_decref(buf, ctx_arg);

                ctx.backend_->submit(ctx, bufcpy);
        } catch (const error &e) {
                return -1;
        }

        return 0;
}

template <class Context>
::rlc_errno methods<Context>::tx_request_cb(::rlc_context *ctx_arg)
{
        auto &ctx = get_ctx<Context>(ctx_arg);
        try {
                ctx.backend_->request(ctx);
        } catch (const error &e) {
                return -1;
        }

        return 0;
}

template <class Context>
void methods<Context>::event_cb(::rlc_context *ctx, const ::rlc_event *event)
{
        return get_ctx<Context>(ctx).event_handler_(*event);
}

template <class Context>
void *methods<Context>::alloc_cb(::rlc_context *ctx, std::size_t size,
                                 alloc_type type)
{
        auto &allocator = get_ctx<Context>(ctx).allocator_;

        switch (type) {
        case RLC_ALLOC_BUF:
                size += sizeof(::rlc_buf);
                break;
        default:
                break;
        }

        auto mem = allocator.allocate(size + sizeof(size));
        std::memcpy(mem, &size, sizeof(size));

        return mem + sizeof(size);
}

template <class Context>
void methods<Context>::dealloc_cb(::rlc_context *ctx, void *memory,
                                  alloc_type type)
{
        auto &allocator = get_ctx<Context>(ctx).allocator_;

        std::byte *ptr = reinterpret_cast<std::byte *>(memory);
        std::size_t size;

        std::memcpy(&size, ptr - sizeof(size), sizeof(size));

        allocator.deallocate(ptr - sizeof(size), size + sizeof(size));
}

struct plat_init {
        plat_init()
        {
                rlc_plat_init();
        }
};
inline plat_init plat_init_inst __attribute__((used));

}; // namespace detail

}; // namespace rlc

#endif /* __cplusplus */

#endif /* RLC_CXX_H__ */
