add_executable(NanoMatchTests 
    test_orderbook.cpp 
    test_seqlock.cpp 
    test_numa.cpp
    test_matching_logic.cpp
    test_memory_pool.cpp
    test_tcp_framer.cpp
)
target_link_libraries(NanoMatchTests PRIVATE GTest::gtest_main)
target_compile_options(NanoMatchTests PRIVATE -Werror)
