# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

add_executable(cpp_coroutine_rpc_example example.cc)

target_link_libraries(cpp_coroutine_rpc_example PRIVATE cpp_coroutine_rpc::cpp_coroutine_rpc)

install(TARGETS cpp_coroutine_rpc_example
        DESTINATION ${CMAKE_INSTALL_BINDIR})