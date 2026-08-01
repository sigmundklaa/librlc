
#include <rlc/rlc.h>

#include "util/backend.hh"
#include "util/mem.hh"
#include "util/proto.hh"

#include "gabs-overrides/timer/timer.hh"

namespace rlc::test
{

using namespace util;

TEST_CASE("test am", "[am]")
{
        gabs_override::timer_ctx timer_ctx(gabs_override::default_resolver);
        ::rlc_context ctx;
        std::queue<buf::pbuf_ptr> queue;
        unsigned int cnt = 0;
        backend::backend back(backend::queue_submitter(queue),
                              backend::request_counter(cnt));
        int status;

        status = ::rlc_init(&ctx, back, mem::alloc, mem::alloc);
        REQUIRE(status == 0);

        REQUIRE(cnt == 0);
        REQUIRE(queue.empty());

        auto full_buf = buf::create(std::string("Full buffer"));

        status = ::rlc_tx(&ctx, full_buf, nullptr);
        REQUIRE(status == 0);

        REQUIRE(cnt == 1);
        REQUIRE(queue.empty());

        cnt = 0;
        auto avail_size = ::gabs_pbuf_size(full_buf) + 3;

        auto remain = ::rlc_tx_avail(&ctx, avail_size);
        REQUIRE(remain == 0);
        REQUIRE(!queue.empty());

        auto [header, data] = proto::am::split_data(queue.front());
        REQUIRE(header == proto::am::header{true, proto::seginfo::ALL, 0});

        status = ::rlc_deinit(&ctx);
        REQUIRE(status == 0);
}

}; // namespace rlc::test
