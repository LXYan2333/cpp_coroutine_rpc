include(GNUInstallDirs)
if(APPLE)
    list(APPEND CMAKE_INSTALL_RPATH "@executable_path/../lib")
elseif(UNIX)
    list(APPEND CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib ${CMAKE_INSTALL_LIBDIR} "$ORIGIN/../lib")
endif()

add_library(cpp_coroutine_rpc_example_rpc_share_info SHARED share_info.cc)
target_link_libraries(cpp_coroutine_rpc_example_rpc_share_info 
    PUBLIC 
    cpp_coroutine_rpc::cpp_coroutine_rpc)

set(CPP_COROUTINE_RPC_EXAMPLE_SHARED_LIBRARY_BASE_ADDRESS "0x300000000000" CACHE STRING "The example's shared library base address. Must be a multiple of page size (usually 4k) and do not conflict with other libraries' map address.")

# Ref:
# https://shadow-file.blogspot.com/2012/10/specifying-preferred-load-addresses-for.html
# https://sourceware.org/bugzilla/show_bug.cgi?id=25207
include(CheckLinkerFlag)
check_linker_flag("CXX" "-Wl,--image-base=${CPP_COROUTINE_RPC_EXAMPLE_SHARED_LIBRARY_BASE_ADDRESS}" LINKER_SUPPORT_IMAGE_BASE)
check_linker_flag("CXX" "-Wl,-Ttext-segment=${CPP_COROUTINE_RPC_EXAMPLE_SHARED_LIBRARY_BASE_ADDRESS}" LINKER_SUPPORT_TTEXT_SEGMENT)

if(LINKER_SUPPORT_IMAGE_BASE)
    target_link_options(cpp_coroutine_rpc_example_rpc_share_info 
        PRIVATE 
        "-Wl,--image-base=${CPP_COROUTINE_RPC_EXAMPLE_SHARED_LIBRARY_BASE_ADDRESS}")
elseif(LINKER_SUPPORT_TTEXT_SEGMENT)
    target_link_options(cpp_coroutine_rpc_example_rpc_share_info 
        PRIVATE 
        "-Wl,-Ttext-segment=${CPP_COROUTINE_RPC_EXAMPLE_SHARED_LIBRARY_BASE_ADDRESS}")
else()
    message(FATAL_ERROR "Failed to set map address. Possible reasons:
1. Invalid CPP_COROUTINE_RPC_EXAMPLE_SHARED_LIBRARY_BASE_ADDRESS
2. The linker does not support --image-base or -Ttext-segment")
endif()

set(CPP_COROUTINE_RPC_EXAMPLE_WORKER_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBEXECDIR}" CACHE STRING "The worker install path, wich will be used by the master to find the worker executables. Default is CMAKE_INSTALL_PREFIX/CMAKE_INSTALL_LIBEXECDIR")
configure_file(worker_install_path.h.in ${PROJECT_BINARY_DIR}/worker_install_path.h @ONLY)
add_executable(cpp_coroutine_rpc_example_master master.cc)
target_link_libraries(cpp_coroutine_rpc_example_master 
    PRIVATE 
    cpp_coroutine_rpc_example_rpc_share_info)
target_sources(cpp_coroutine_rpc_example_master
    PRIVATE
    FILE_SET HEADERS
    BASE_DIRS ${PROJECT_BINARY_DIR}
    FILES ${PROJECT_BINARY_DIR}/worker_install_path.h)

add_executable(cpp_coroutine_rpc_example_worker_pi worker_pi.cc)
target_link_libraries(cpp_coroutine_rpc_example_worker_pi 
    PRIVATE 
    cpp_coroutine_rpc_example_rpc_share_info)

add_executable(cpp_coroutine_rpc_example_worker_e worker_e.cc)
target_link_libraries(cpp_coroutine_rpc_example_worker_e
    PRIVATE 
    cpp_coroutine_rpc_example_rpc_share_info)

install(TARGETS cpp_coroutine_rpc_example_rpc_share_info
                cpp_coroutine_rpc_example_master
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS cpp_coroutine_rpc_example_worker_pi
                cpp_coroutine_rpc_example_worker_e
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
