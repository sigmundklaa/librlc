
#ifndef RLC_GABS_OVERRIDES_TIMER_HH__
#define RLC_GABS_OVERRIDES_TIMER_HH__

#include <functional>
#include <chrono>
#include <utility>
#include <atomic>
#include <mutex>
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

        /*
         * Test-only extension, deliberately *not* what gabs_timer_active()
         * reports: stopping a timer is asynchronous in the gabs API, so
         * active() keeps its joinable()-based meaning. This flag instead
         * records whether the timer has been *asked* to be running - set by
         * start()/restart(), cleared by stop() and by a resolver's callback
         * once it fires (one-shot) - which is what a test needs to assert
         * on synchronously.
         */
        std::atomic<bool> armed{false};

        /* Used by manual_resolver/fire() below to let a test drive this
         * timer's callback synchronously, without waiting on real time. */
        std::mutex fire_mutex;
        std::condition_variable_any fire_cv;
        bool fire_requested = false;
        bool fire_complete = false;

        /*
         * Declared last so it is destroyed *first* (member destruction runs
         * in reverse declaration order): runner's destructor stops and
         * joins the thread before fire_mutex/fire_cv/active go away, since
         * a running resolver callback may still be using them.
         */
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

                t->armed.store(true);

                {
                        std::lock_guard<std::mutex> lock(t->fire_mutex);
                        t->fire_requested = false;
                        t->fire_complete = false;
                }

                t->runner = std::jthread(resolve(handle), t, delay);
        }

        void stop(void *handle)
        {
                auto t = reinterpret_cast<timer *>(handle);

                t->armed.store(false);
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

        /**
         * @brief Test-only: whether the timer is currently *meant* to be
         * running, updated synchronously by start()/stop()/fire().
         *
         * gabs_timer_active() cannot answer this - stopping is
         * asynchronous, so a stopped timer still reports active until its
         * thread is joined.
         */
        bool armed(void *handle)
        {
                auto t = reinterpret_cast<timer *>(handle);

                return t->armed.load();
        }

        /**
         * @brief Drive `handle`'s callback to run now, blocking until it has
         * finished (or a concurrent stop() wins the race).
         *
         * Only meaningful for a timer started with a resolver whose
         * callback waits on `fire_requested` (see manual_resolver).
         */
        void fire(void *handle)
        {
                auto t = reinterpret_cast<timer *>(handle);

                if (!t->armed.load()) {
                        throw std::logic_error(
                                "fire() called on a timer that is not "
                                "armed (never started, already stopped, "
                                "or already fired)");
                }

                {
                        std::lock_guard<std::mutex> lock(t->fire_mutex);
                        t->fire_requested = true;
                }
                t->fire_cv.notify_all();

                std::unique_lock<std::mutex> lock(t->fire_mutex);
                t->fire_cv.wait(lock, [t] { return t->fire_complete; });
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
                t->armed.store(false);
        }
}

inline timer_ctx::callback_type default_resolver(void *)
{
        return default_timer;
}

/**
 * @brief Resolver whose timers never fire on their own. A test drives them
 * explicitly via timer_ctx::fire()/fire(gabs_timer), synchronously and
 * without depending on real elapsed time.
 */
inline void manual_timer(std::stop_token stop_token, timer *t,
                         std::chrono::microseconds /*delay*/)
{
        std::unique_lock<std::mutex> lock(t->fire_mutex);

        t->fire_cv.wait(lock, stop_token, [t] { return t->fire_requested; });

        if (!stop_token.stop_requested()) {
                lock.unlock();
                t->cb(t);
                t->armed.store(false);
                lock.lock();
        }

        t->fire_complete = true;
        lock.unlock();
        t->fire_cv.notify_all();
}

inline timer_ctx::callback_type manual_resolver(void *)
{
        return manual_timer;
}

inline void fire(gabs_timer handle)
{
        timer_ctx::get_inst()->fire(handle);
}

inline bool armed(gabs_timer handle)
{
        return timer_ctx::get_inst()->armed(handle);
}

}; // namespace rlc::gabs_override

#endif /* RLC_GABS_OVERRIDES_TIMER_HH__ */
