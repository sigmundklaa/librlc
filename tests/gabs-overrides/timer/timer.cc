
#include <assert.h>

#include <gabs/timer.h>

#include "timer.hh"

using namespace rlc::gabs_override;

extern "C" {

int gabs_timer_ctx_init(gabs_timer_ctx *ctx)
{
        auto ptr = reinterpret_cast<timer_ctx **>(ctx);

        *ptr = timer_ctx::get_inst();
        return 0;
}

int gabs_timer_ctx_deinit(gabs_timer_ctx *ctx)
{
        return 0;
}

bool gabs_timer_okay(gabs_timer t)
{
        return t != nullptr;
}

gabs_timer gabs_timer_install(gabs_timer_ctx *ctx, gabs_timer_cb cb,
                              void *user_data)
{
        assert(*reinterpret_cast<timer_ctx **>(ctx) == timer_ctx::get_inst());

        return timer_ctx::get_inst()->add(
                [cb, user_data](gabs_timer t) { cb(t, user_data); });
}

int gabs_timer_uninstall(gabs_timer t)
{
        timer_ctx::get_inst()->remove(t);
        return 0;
}

int gabs_timer_start(gabs_timer t, uint64_t delay_us)
{
        timer_ctx::get_inst()->start(t, std::chrono::microseconds(delay_us));
        return 0;
}

int gabs_timer_restart(gabs_timer t, uint64_t delay_us)
{
        timer_ctx::get_inst()->restart(t, std::chrono::microseconds(delay_us));
        return 0;
}

int gabs_timer_stop(gabs_timer t)
{
        timer_ctx::get_inst()->stop(t);
        return 0;
}

bool gabs_timer_active(gabs_timer t)
{
        return timer_ctx::get_inst()->active(t);
}
};
