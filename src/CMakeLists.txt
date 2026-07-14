add_executable(NanoMatchServer main.cpp TcpServer.cpp)

# Platform-conditional linking
if(WIN32)
    target_link_libraries(NanoMatchServer PRIVATE ws2_32)
else()
    target_link_libraries(NanoMatchServer PRIVATE pthread)
endif()

if(NOT MSVC)
    target_compile_options(NanoMatchServer PRIVATE -Werror)
endif()
