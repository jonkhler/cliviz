add_executable(cliviz_tests
    outbuf_test.cpp
    term_test.cpp
    cell_test.cpp
    framebuf_test.cpp
    pixbuf_test.cpp
    integration_test.cpp
    color_test.cpp
)

target_link_libraries(cliviz_tests PRIVATE cliviz_static GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(cliviz_tests)
