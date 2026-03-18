add_executable(cliviz_tests
    smoke_test.cpp
)

target_link_libraries(cliviz_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(cliviz_tests)
