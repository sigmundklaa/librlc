
#include <chrono>
#include <vector>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <thread>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <rlc/rlcxx.h>

namespace
{

using buffer = std::shared_ptr<std::vector<std::byte>>;
using buffer_collection = std::vector<buffer>;

struct radio_driver {
        virtual ~radio_driver()
        {
        }

        virtual void send(const buffer_collection &data) = 0;
        virtual buffer recv() = 0;
};

class radio_manager
{
      public:
        struct channel {
                std::shared_ptr<rlc::context<>> ctx;
        };

        radio_manager(std::size_t mtu, std::shared_ptr<radio_driver> radio)
                : mtu(mtu), radio(radio),
                  tx_handle(&radio_manager::tx_thread, this),
                  rx_handle(&radio_manager::rx_thread, this)
        {
        }

        void add_channel(channel &&chan)
        {
                std::unique_lock guard(chan_lock);
                chans.emplace_back(std::move(chan));
        }

        void request()
        {
                std::unique_lock guard(delay_lock);
                tx_schedule_locked(std::chrono::steady_clock::now());
        }

        void submit(const buffer &data)
        {
                std::unique_lock guard(q_lock);
                bufs.push_back(data);
        }

      private:
        using time_point = std::chrono::time_point<std::chrono::steady_clock>;

        void tx_schedule_locked(time_point tp)
        {
                if (!deadline.has_value() || *deadline < tp) {
                        deadline = tp;
                        cv.notify_one();
                }
        }

        void tx_schedule(time_point tp)
        {
                std::unique_lock guard(delay_lock);
                tx_schedule_locked(tp);
        }

        void tx_wait()
        {
                std::unique_lock guard(delay_lock);
                std::cv_status res;
                do {
                        if (deadline.has_value()) {
                                res = cv.wait_until(guard, *deadline);
                        } else {
                                cv.wait(guard);
                                res = std::cv_status::no_timeout;
                        }
                } while (res != std::cv_status::timeout);

                deadline.reset();
        }

        std::size_t tx_avail(std::size_t max_bytes)
        {
                std::unique_lock guard(chan_lock);
                std::size_t remaining = max_bytes;

                for (auto &chan : chans) {
                        remaining = chan.ctx->tx_avail(remaining);
                }

                return remaining;
        }

        void tx_thread()
        {
                using namespace std::chrono;

                auto tx_delay = microseconds(200);

                for (;;) {
                        tx_wait();
                        auto remaining = tx_avail(mtu);

                        if (remaining != mtu) {
                                std::unique_lock qguard(q_lock);
                                radio->send(bufs);
                                bufs.clear();
                        }

                        if (remaining > 0) {
                                tx_schedule(steady_clock::now() + tx_delay);
                        }
                }
        }

        void rx_thread()
        {
                for (;;) {
                        buffer buf = radio->recv();
                        if (buf->size() == 0) {
                                continue;
                        }

                        /* TODO: strip header */
                        std::unique_lock guard(chan_lock);
                        chans.front().ctx->rx_submit(buf);
                }
        }

        std::thread tx_handle;
        std::thread rx_handle;
        std::shared_ptr<radio_driver> radio;
        std::size_t mtu;
        std::mutex delay_lock;
        std::mutex q_lock;
        std::mutex chan_lock;
        std::condition_variable cv;
        std::vector<channel> chans;
        std::optional<time_point> deadline;
        buffer_collection bufs;
};

template <class Context = rlc::context<>>
struct backend : public rlc::backend<Context> {
        std::shared_ptr<radio_manager> radio;

        backend(const std::shared_ptr<radio_manager> radio) : radio(radio)
        {
        }

        void submit(const Context &ctx,
                    const typename Context::buffer_type &data) const override
        {
                radio->submit(data);
        }

        void request(const Context &ctx) const override
        {
                radio->request();
        }
};

class socket_radio : public radio_driver
{
      public:
        socket_radio(const std::string &path, std::size_t buf_size = 4096)
                : buf_size(buf_size)
        {
                ::sockaddr_un addr;
                ::sockaddr_un cli;
                ::socklen_t clilen;

                addr.sun_family = AF_UNIX;
                std::strcpy(addr.sun_path, path.c_str());

                sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (sock < 0) {
                        throw std::runtime_error("Unable to create socket");
                }

                if (::bind(sock, (::sockaddr *)&addr, sizeof(addr)) < 0) {
                        throw std::runtime_error("Connection failed");
                }

                if (::listen(sock, 10) < 0) {
                        throw std::runtime_error(
                                std::string("Listen failed: ") +
                                std::strerror(errno));
                }

                fd = ::accept(sock, (::sockaddr *)&cli, &clilen);
                if (fd < 0) {
                        throw std::runtime_error(
                                std::string("Accept failed: ") +
                                std::strerror(errno));
                }
        }

        ~socket_radio()
        {
                ::close(fd);
                ::close(sock);
        }

        void send(const buffer_collection &data) override
        {
                for (const auto &buf : data) {
                        std::ptrdiff_t bytes = 0;

                        do {
                                auto ret = ::write(fd, buf->data() + bytes,
                                                   buf->size());
                                if (ret < 0) {
                                        throw std::runtime_error(
                                                std::string("Write error: ") +
                                                std::strerror(ret));
                                }
                        } while (bytes < buf->size());
                }
        }

        buffer recv() override
        {
                auto buf = std::make_shared<buffer::element_type>(buf_size);
                auto ret = ::read(fd, buf->data(), buf->size());
                if (ret < 0) {
                        throw std::runtime_error(std::string("Read error: ") +
                                                 std::strerror(ret));
                }

                buf->resize(ret);
                return buf;
        }

      private:
        int sock;
        int fd;
        std::size_t buf_size;
};

}; // namespace

int main(int argc, char **argv)
{
        if (argc < 2) {
                std::cout << "Usage: " << argv[0] << " <socket_path>"
                          << std::endl;
                return 1;
        }

        auto radio = std::make_shared<socket_radio>(argv[1]);
        auto radio_man = std::make_shared<radio_manager>(256, radio);
        auto back = std::make_shared<backend<>>(radio_man);
        auto conf = std::make_shared<rlc::config>((rlc::config){
                .window_size = 5,
                .buffer_size = 1500,
                .pdu_without_poll_max = 5,
                .byte_without_poll_max = 500,
                .time_reassembly_us = 500000,
                .time_poll_retransmit_us = 50000,
                .max_retx_threshhold = 10,
                .sn_width = RLC_SN_18BIT,
        });
        auto eh = [&](const rlc::event &event) {};
        auto ctx = std::make_shared<rlc::context<>>(RLC_AM, eh, back, conf);

        radio_man->add_channel({ctx});

        for (;;) {
                try {
                        ctx->send(std::make_shared<buffer::element_type>(4000));
                } catch (const rlc::error &err) {
                        std::cout << err.what() << std::endl;
                }

                std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return 0;
}
