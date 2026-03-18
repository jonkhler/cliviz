add_executable(cliviz_tests
    smoke_test.cpp
    outbuf_test.cpp
    term_test.cpp
    cell_test.cpp
    framebuf_test.cpp
    ${CMAKE_SOURCE_DIR}/src/term.cpp
    ${CMAKE_SOURCE_DIR}/src/framebuf.cpp
)

target_include_directories(cliviz_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cliviz_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(cliviz_tests)
