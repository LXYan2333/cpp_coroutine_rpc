add_executable(cpp_coroutine_rpc_example example.cc)

target_link_libraries(cpp_coroutine_rpc_example PRIVATE cpp_coroutine_rpc::cpp_coroutine_rpc)

install(TARGETS cpp_coroutine_rpc_example
        DESTINATION ${CMAKE_INSTALL_BINDIR})