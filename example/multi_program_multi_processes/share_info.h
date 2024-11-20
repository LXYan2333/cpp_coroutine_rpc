/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <cpp_coroutine_rpc/fixed_identical_address.h>
#include <semaphore>

namespace my_test {

auto calculate_pi() -> double;
auto calculate_e() -> double;

auto calculate_pi_e(
    cpp_coroutine_rpc::fixed_identical_address::rpc_context *context,
    double *pi, double *e) -> cpp_coroutine_rpc::fixed_identical_address::task;

auto notify_finish(
    cpp_coroutine_rpc::fixed_identical_address::rpc_context *context)
    -> cpp_coroutine_rpc::fixed_identical_address::task;

const inline volatile auto prevent_optimize_out = &my_test::calculate_pi_e;

struct local_context {
  std::binary_semaphore finished{0};
};

} // namespace my_test