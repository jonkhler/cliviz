cmake_minimum_required(VERSION 3.20)
project(cliviz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# ── Core library: terminal renderer ──

add_library(cliviz_static STATIC
    csrc/src/term.cpp
    csrc/src/framebuf.cpp
    csrc/src/pixbuf.cpp
)
target_include_directories(cliviz_static PUBLIC csrc/include/cliviz csrc/include)

# ── Examples (skip when building Python wheel) ──

if(NOT SKBUILD)
    add_executable(fire csrc/examples/fire.cpp)
    target_link_libraries(fire PRIVATE cliviz_static)

    add_executable(starfield csrc/examples/starfield.cpp)
    target_link_libraries(starfield PRIVATE cliviz_static)
endif()

# ── Tests (FetchContent for GTest, skip during wheel build) ──

if(NOT SKBUILD)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(CMAKE_CXX_FLAGS_SAVED "${CMAKE_CXX_FLAGS}")
    string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    FetchContent_MakeAvailable(googletest)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_SAVED}")

    enable_testing()
    add_subdirectory(csrc/tests)
endif()

# ── Python bindings (nanobind) ──

find_package(Python COMPONENTS Interpreter Development.Module QUIET)
find_package(nanobind CONFIG QUIET)
if(Python_FOUND AND nanobind_FOUND)
    nanobind_add_module(_native python/cliviz/_native.cpp)
    target_link_libraries(_native PRIVATE cliviz_static)
    install(TARGETS _native LIBRARY DESTINATION cliviz)
endif()

# ── Install ──

include(GNUInstallDirs)
install(TARGETS cliviz_static
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY csrc/include/cliviz/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cliviz)
