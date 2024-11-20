# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(cpp_coroutine_rpc VERSION 1.0.0 LANGUAGES CXX)

add_library(cpp_coroutine_rpc INTERFACE)
add_library(cpp_coroutine_rpc::cpp_coroutine_rpc ALIAS cpp_coroutine_rpc)
target_compile_features(cpp_coroutine_rpc INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

find_package(Boost REQUIRED COMPONENTS headers)

target_link_libraries(cpp_coroutine_rpc INTERFACE Boost::headers)
target_sources(
    cpp_coroutine_rpc
    INTERFACE
    FILE_SET    HEADERS
    BASE_DIRS   ${PROJECT_SOURCE_DIR}/include
    FILES       ${PROJECT_SOURCE_DIR}/include/cpp_coroutine_rpc/cpp_coroutine_rpc.h
    FILES       ${PROJECT_SOURCE_DIR}/include/cpp_coroutine_rpc/fixed_identical_address.h)

include(GNUInstallDirs)
install(TARGETS cpp_coroutine_rpc
        EXPORT  cpp_coroutine_rpcTargets
        FILE_SET HEADERS)

option(CPP_COROUTINE_RPC_BUILD_EXAMPLES "Build cpp_coroutine_rpc examples" OFF)

if(CPP_COROUTINE_RPC_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

install(EXPORT cpp_coroutine_rpcTargets
    DESTINATION lib/cmake/cpp_coroutine_rpc
    NAMESPACE cpp_coroutine_rpc::
    FILE cpp_coroutine_rpcTargets.cmake)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/cpp_coroutine_rpcConfig.cmake.in
    ${PROJECT_BINARY_DIR}/cpp_coroutine_rpcConfig.cmake
    INSTALL_DESTINATION lib/cmake/cpp_coroutine_rpc
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO)
    
write_basic_package_version_file(
    cpp_coroutine_rpcConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion)

install(FILES ${PROJECT_BINARY_DIR}/cpp_coroutine_rpcConfig.cmake
              ${PROJECT_BINARY_DIR}/cpp_coroutine_rpcConfigVersion.cmake
        DESTINATION lib/cmake/cpp_coroutine_rpc)

export(EXPORT cpp_coroutine_rpcTargets
    NAMESPACE cpp_coroutine_rpc::
    FILE "${PROJECT_BINARY_DIR}/cpp_coroutine_rpcTargets.cmake")

# has no effect unless CMAKE_EXPORT_PACKAGE_REGISTRY is set to ON
export(PACKAGE cpp_coroutine_rpc)