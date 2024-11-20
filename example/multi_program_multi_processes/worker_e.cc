/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "share_info.h"
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <numbers>
#include <thread>

namespace bip = boost::interprocess;
using namespace std::literals;

namespace my_test {

auto calculate_e() -> double {
  // pretent to do some heavy work
  std::this_thread::sleep_for(1s);
  return std::numbers::e;
};

} // namespace my_test

auto main() -> int {

  // NOLINTBEGIN(*-reinterpret-cast)
  bip::fixed_managed_shared_memory segment(
      bip::open_only, "test_shared_info",
      reinterpret_cast<void *>(0x400000000000));
  // NOLINTEND(*-reinterpret-cast)

  bip::named_semaphore start_semaphore(bip::open_only, "test_start_semaphore");
  start_semaphore.post();

  my_test::local_context ctx{};
  cpp_coroutine_rpc::fixed_identical_address::rpc_context context{
      &segment, "test", "e_calculator", &ctx, 2, true};
  std::jthread worker([&](const std::stop_token &stoken) {
    while (!stoken.stop_requested()) {
      context.listen_once();
      std::this_thread::sleep_for(10ms);
    }
  });

  ctx.finished.acquire();
  return 0;
}