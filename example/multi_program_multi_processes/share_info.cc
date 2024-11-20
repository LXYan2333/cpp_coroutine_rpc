/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "share_info.h"
#include <exception>
#include <iostream>

namespace my_test {

__attribute__((weak)) auto calculate_pi() -> double {
  std::cerr << "this function should not be called on this process\n";
  std::terminate();
};
__attribute__((weak)) auto calculate_e() -> double {
  std::cerr << "this function should not be called on this process\n";
  std::terminate();
};

auto calculate_pi_e(
    cpp_coroutine_rpc::fixed_identical_address::rpc_context *context,
    double *pi, double *e) -> cpp_coroutine_rpc::fixed_identical_address::task {
  double m_pi{};
  double m_e{};

  context = co_await context->resume_on("pi_calculator");
  std::cerr << "on pi_calculator, pid: " << getpid() << '\n';
  m_pi = calculate_pi();

  context = co_await context->resume_on("e_calculator");
  std::cerr << "on e_calculator, pid: " << getpid() << '\n';
  m_e = calculate_e();

  context = co_await context->resume_on("master");
  std::cerr << "on master, pid: " << getpid() << '\n';
  *pi = m_pi;
  *e = m_e;
}

auto notify_finish(
    cpp_coroutine_rpc::fixed_identical_address::rpc_context *context)
    -> cpp_coroutine_rpc::fixed_identical_address::task {

  context = co_await context->resume_on("pi_calculator");
  std::cerr << "stop pi_calculator\n";
  auto *local_ctx = static_cast<local_context *>(context->get_local_context());
  local_ctx->finished.release();

  context = co_await context->resume_on("e_calculator");
  std::cerr << "stop e_calculator\n";
  local_ctx = static_cast<local_context *>(context->get_local_context());
  local_ctx->finished.release();
};

} // namespace my_test