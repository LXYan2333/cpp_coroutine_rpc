#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/detail/segment_manager_helper.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <cpp_coroutine_rpc/fixed_identical_address.h>
#include <cstdio>
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

auto get_pid(cpp_coroutine_rpc::fixed_identical_address::rpc_context *context)
    -> cpp_coroutine_rpc::fixed_identical_address::task {

  cpp_coroutine_rpc::fixed_identical_address::segment_memory_resource resource(
      context);
  std::pmr::polymorphic_allocator<> alloc(&resource);
  std::vector<int, std::pmr::polymorphic_allocator<int>> vec(alloc);
  fprintf(stderr, "master pid: %d\n", getpid());
  vec.push_back(getpid());
  context = co_await context->resume_on("target");

  fprintf(stderr, "success! worker pid: %d\n", getpid());
  vec.push_back(getpid());
  fprintf(stderr, "vec: %d, %d\n", vec[0], vec[1]);
  vec.reserve(6);
  context = co_await context->resume_on("main");
}

auto call_stop(cpp_coroutine_rpc::fixed_identical_address::rpc_context *context)
    -> cpp_coroutine_rpc::fixed_identical_address::task {
  context = co_await context->resume_on("target");
  auto *local_ctx = static_cast<local_context *>(context->get_local_context());
  local_ctx->finished.release();
}

int main() {

  using namespace boost::interprocess;
  printf("parent process, PID: %d\n", getpid());
  fflush(stdout);
  // Remove shared memory on construction and destruction
  struct shm_remove {
    shm_remove() { shared_memory_object::remove("MySharedMemory"); }
    ~shm_remove() { shared_memory_object::remove("MySharedMemory"); }
  } remover;

  cpp_coroutine_rpc::fixed_identical_address::rpc_context::remove_named_objects(
      "test", "main");
  cpp_coroutine_rpc::fixed_identical_address::rpc_context::remove_named_objects(
      "test", "target");

  // Create a managed shared memory segment
  fixed_managed_shared_memory segment(create_only, "MySharedMemory", 65536,
                                      (void *)0x400000000000);
  auto *start_semaphore =
      segment.construct<interprocess_semaphore>(bip::anonymous_instance)(0);

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    return 1;
  }
  if (pid != 0) { // Parent process
    printf("child process, PID: %d\n", pid);
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