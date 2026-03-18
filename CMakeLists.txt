cmake_minimum_required(VERSION 3.20)
project(cliviz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

add_library(cliviz_lib STATIC
    src/term.cpp
    src/framebuf.cpp
    src/pixbuf.cpp
    src/raster.cpp
    src/sdf.cpp
)
target_include_directories(cliviz_lib PUBLIC src)

add_executable(cliviz src/main.cpp)
target_link_libraries(cliviz PRIVATE cliviz_lib)

find_package(GTest REQUIRED)

enable_testing()
add_subdirectory(tests)
