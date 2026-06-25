add_executable(NanoMatchServer main.cpp TcpServer.cpp)
target_link_libraries(NanoMatchServer PRIVATE ws2_32)
target_compile_options(NanoMatchServer PRIVATE -Werror)
