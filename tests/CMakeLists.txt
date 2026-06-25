add_executable(NanoMatchTests test_orderbook.cpp test_seqlock.cpp test_numa.cpp)
target_link_libraries(NanoMatchTests PRIVATE GTest::gtest_main)
target_compile_options(NanoMatchTests PRIVATE -Werror)
