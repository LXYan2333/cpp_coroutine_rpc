/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "share_info.h"
#include <array>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <cpp_coroutine_rpc/fixed_identical_address.h>
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <worker_install_path.h>

namespace bip = boost::interprocess;
using namespace std::literals;

auto main() -> int {
  struct remover { // NOLINT(*-special-member-functions)
    remover() {
      bip::shared_memory_object::remove("test_shared_info");
      bip::named_semaphore::remove("test_start_semaphore");
    }
    ~remover() {
      bip::shared_memory_object::remove("test_shared_info");
      bip::named_semaphore::remove("test_start_semaphore");
    }
  } remove;

  cpp_coroutine_rpc::fixed_identical_address::rpc_context::remove_named_objects(
      "test", "master");
  cpp_coroutine_rpc::fixed_identical_address::rpc_context::remove_named_objects(
      "test", "pi_calculator");
  cpp_coroutine_rpc::fixed_identical_address::rpc_context::remove_named_objects(
      "test", "e_calculator");

  // NOLINTBEGIN(*-reinterpret-cast)
  bip::fixed_managed_shared_memory segment(
      bip::create_only, "test_shared_info",
      static_cast<size_t>(1024) * 4 * 1024,
      reinterpret_cast<void *>(0x400000000000));
  // NOLINTEND(*-reinterpret-cast)

  {
    cpp_coroutine_rpc::fixed_identical_address::rpc_context context{
        &segment, "test", "master", nullptr, 2, true};
    double pi{};
    double e{};
    auto pi_e = my_test::calculate_pi_e(&context, &pi, &e);

    bip::named_semaphore start_semaphore(bip::create_only,
                                         "test_start_semaphore", 0);
    // run worker_pi and wait for it to start
    pid_t pid_pi = fork();
    if (pid_pi == -1) {
      perror("fork");
      return 1;
    } else if (pid_pi == 0) {
      std::string cmd =
          WORKER_INSTALL_PATH "/cpp_coroutine_rpc_example_worker_pi";
      std::array<char *, 2> argv = {cmd.data(), nullptr};
      if (execvp(cmd.data(), argv.data()) == -1) {
        perror("execvp");
        std::cerr << " : " << cmd << '\n';
        std::cerr << "You may need to set CMAKE_INSTALL_PREFIX or"
                     "CPP_COROUTINE_RPC_EXAMPLE_WORKER_INSTALL_PATH to the "
                     "correct install path\n";
        return 1;
      };
    }
    start_semaphore.wait();

    // run worker_e and wait for it to start
    pid_t pid_e = fork();
    if (pid_e == -1) {
      perror("fork");
      return 1;
    } else if (pid_e == 0) {
      std::string cmd =
          WORKER_INSTALL_PATH "/cpp_coroutine_rpc_example_worker_e";
      std::array<char *, 2> argv = {cmd.data(), nullptr};
      if (execvp(cmd.data(), argv.data()) == -1) {
        perror("execvp");
        std::cerr << " : " << cmd << '\n';
        std::cerr << "You may need to set CMAKE_INSTALL_PREFIX or"
                     "CPP_COROUTINE_RPC_EXAMPLE_WORKER_INSTALL_PATH to the "
                     "correct install path\n";
        return 1;
      };
    }
    start_semaphore.wait();

    // start another thread to listen to the message from other processes
    std::jthread worker([&](const std::stop_token &stoken) {
      while (!stoken.stop_requested()) {
        context.listen_once();
        std::this_thread::sleep_for(10ms);
      }
    });

    pi_e.start();
    pi_e.wait();

    std::cout << "pi: " << pi << ", e: " << e << '\n';

    // stop the worker_pi and worker_e
    auto call_stop = my_test::notify_finish(&context);
    call_stop.start();
    call_stop.wait();

    waitpid(pid_pi, nullptr, 0);
    waitpid(pid_e, nullptr, 0);
  } // context destructed

  // check all resources are released
  assert(segment.get_num_named_objects() == 0);
  assert(segment.get_num_unique_objects() == 0);

  return 0;
}