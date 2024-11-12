#include "share_info.h"
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <numbers>
#include <thread>

namespace bip = boost::interprocess;
using namespace std::literals;

namespace my_test {

auto calculate_pi() -> double {
  // pretent to do some heavy work
  std::this_thread::sleep_for(1s);
  return std::numbers::pi;
};

} // namespace my_test

auto main() -> int {
  bip::fixed_managed_shared_memory segment(
      bip::open_only, "test_shared_info",
      reinterpret_cast<void *>(0x400000000000));
  bip::named_semaphore start_semaphore(bip::open_only, "test_start_semaphore");
  start_semaphore.post();

  my_test::local_context ctx{};
  cpp_coroutine_rpc::fixed_identical_address::rpc_context context{
      &segment, "test", "pi_calculator", &ctx, 2, true};
  std::jthread worker([&](const std::stop_token &stoken) {
    while (!stoken.stop_requested()) {
      context.listen_once();
      std::this_thread::sleep_for(10ms);
    }
  });

  ctx.finished.acquire();
  return 0;
}