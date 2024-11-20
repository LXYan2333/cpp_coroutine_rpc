/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/detail/segment_manager_helper.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <cpp_coroutine_rpc/fixed_identical_address.h>
#include <iostream>
#include <memory_resource>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace bip = boost::interprocess;

using namespace std::literals;

struct local_context {
  std::binary_semaphore finished{0};
};

namespace {
auto get_pid(cpp_coroutine_rpc::fixed_identical_address::rpc_context *context)
    -> cpp_coroutine_rpc::fixed_identical_address::task {

  cpp_coroutine_rpc::fixed_identical_address::segment_memory_resource resource(
      context);
  std::pmr::polymorphic_allocator<> alloc(&resource);
  std::vector<int, std::pmr::polymorphic_allocator<int>> vec(alloc);
  std::cerr << "master pid: " << getpid() << '\n';
  vec.push_back(getpid());
  context = co_await context->resume_on("target");

  std::cerr << "success! worker pid: " << getpid() << '\n';
  vec.push_back(getpid());
  std::cerr << "vec: " << vec[0] << ", " << vec[1] << '\n';

  // just show dynamic memory allocation works in this coroutine
  vec.reserve(8);

  context = co_await context->resume_on("main");
}
auto call_stop(cpp_coroutine_rpc::fixed_identical_address::rpc_context *context)
    -> cpp_coroutine_rpc::fixed_identical_address::task {
  context = co_await context->resume_on("target");
  auto *local_ctx = static_cast<local_context *>(context->get_local_context());
  local_ctx->finished.release();
}
} // namespace

int main() {

  using namespace boost::interprocess;
  std::cerr << "parent process, PID: " << getpid() << '\n';
  // Remove shared memory on construction and destruction
  struct shm_remove { // NOLINT(*-special-member-functions)
    shm_remove() { shared_memory_object::remove("MySharedMemory"); }
    ~shm_remove() { shared_memory_object::remove("MySharedMemory"); }
  } remover;

  cpp_coroutine_rpc::fixed_identical_address::rpc_context::remove_named_objects(
      "test", "main");
  cpp_coroutine_rpc::fixed_identical_address::rpc_context::remove_named_objects(
      "test", "target");

  // Create a managed shared memory segment
  // NOLINTBEGIN(*-reinterpret-cast)
  fixed_managed_shared_memory segment(create_only, "MySharedMemory", 65536,
                                      reinterpret_cast<void *>(0x400000000000));
  // NOLINTEND(*-reinterpret-cast)

  auto *start_semaphore =
      segment.construct<interprocess_semaphore>(bip::anonymous_instance)(0);

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    return 1;
  }
  if (pid != 0) { // child process
    std::cerr << "child process, PID: " << pid << '\n';
    cpp_coroutine_rpc::fixed_identical_address::rpc_context context{
        &segment, "test", "main", nullptr, 2, true};
    start_semaphore->wait();
    std::jthread worker([&](const std::stop_token &stoken) {
      while (!stoken.stop_requested()) {
        context.listen_once();
        std::this_thread::sleep_for(10ms);
      }
    });

    auto coro = get_pid(&context);
    coro.start();
    coro.wait();

    auto stop_coro = call_stop(&context);
    stop_coro.start();
    stop_coro.wait();
    wait(nullptr);
  } else {
    local_context ctx;
    cpp_coroutine_rpc::fixed_identical_address::rpc_context context{
        &segment, "test", "target", &ctx, 2, true};
    start_semaphore->post();
    std::jthread worker([&](const std::stop_token &stoken) {
      while (!stoken.stop_requested()) {
        context.listen_once();
        std::this_thread::sleep_for(10ms);
      }
    });

    // do whatever you want
    // …………

    ctx.finished.acquire();
  }

  if (pid != 0) {
    assert(segment.get_num_named_objects() == 0);
    assert(segment.get_num_unique_objects() == 0);
  }
  return 0;
}