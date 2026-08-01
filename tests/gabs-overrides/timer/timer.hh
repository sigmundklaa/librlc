
#ifndef RLC_GABS_OVERRIDES_TIMER_HH__
#define RLC_GABS_OVERRIDES_TIMER_HH__

#include <functional>
#include <chrono>
#include <utility>
#include <condition_variable>
#include <thread>
#include <stop_token>
#include <list>

#include "gabs_timer_def.h"

namespace rlc::gabs_override
{

struct timer {
        using fire_fn = std::function<void(gabs_timer)>;

        fire_fn cb;
        std::jthread runner;
};

class timer_ctx
{
      public:
        using callback_type = std::function<void(std::stop_token, timer *,
                                                 std::chrono::microseconds)>;
        using resolve_func = std::function<callback_type(gabs_timer)>;

        timer_ctx(const resolve_func &resolver) : resolver(resolver)
        {
                if (timer_ctx::inst != nullptr) {
                        throw std::logic_error("Instance already exists");
                }

                timer_ctx::inst = this;
        }

        ~timer_ctx()
        {
                timer_ctx::inst = nullptr;
        }

        callback_type resolve(void *handle)
        {
                return resolver(handle);
        }

        void *add(const timer::fire_fn &fn)
        {
                timers.emplace_back(fn);
                return &timers.back();
        }

        void remove(void *handle)
        {
                for (auto it = timers.begin(); it != timers.end(); it++) {
                        if (&*it == handle) {
                                if (it->runner.joinable()) {
                                        it->runner.request_stop();
                                        it->runner.join();
                                }

                                timers.erase(it);
                                return;
                        }
                }
        }

        void start(void *handle, std::chrono::microseconds delay)
        {
                auto t = reinterpret_cast<timer *>(handle);

                t->runner = std::jthread(resolve(handle), t, delay);
        }

        void stop(void *handle)
        {
                auto t = reinterpret_cast<timer *>(handle);

                t->runner.request_stop();
        }

        void restart(void *handle, std::chrono::microseconds delay)
        {
                start(handle, delay);
        }

        bool active(void *handle)
        {
                auto t = reinterpret_cast<timer *>(handle);

                return t->runner.joinable();
        }

        static timer_ctx *get_inst()
        {
                if (timer_ctx::inst == nullptr) {
                        throw std::logic_error("No instance registered");
                }

                return timer_ctx::inst;
        }

      private:
        static inline timer_ctx *inst;
        std::list<timer> timers;

        resolve_func resolver;
};

inline void default_timer(std::stop_token stop_token, void *t_arg,
                          std::chrono::microseconds delay)
{
        std::condition_variable_any cv;
        std::mutex lock;
        std::unique_lock guard(lock);
        auto t = reinterpret_cast<timer *>(t_arg);

        auto stopped = cv.wait_for(guard, stop_token, delay, [&stop_token]() {
                return stop_token.stop_requested();
        });

        if (!stopped) {
                t->cb(t);
        }
}

inline timer_ctx::callback_type default_resolver(void *)
{
        return default_timer;
}

}; // namespace rlc::gabs_override

#endif /* RLC_GABS_OVERRIDES_TIMER_HH__ */
