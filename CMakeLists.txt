cmake_minimum_required(VERSION 3.20)
project(cliviz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# ── Library (static + shared) ──

set(CLIVIZ_SOURCES
    src/term.cpp
    src/framebuf.cpp
    src/pixbuf.cpp
    src/raster.cpp
    src/sdf.cpp
    src/threadpool.cpp
    src/cliviz_api.cpp
)

add_library(cliviz_static STATIC ${CLIVIZ_SOURCES})
target_include_directories(cliviz_static PUBLIC include/cliviz include)

add_library(cliviz_shared SHARED ${CLIVIZ_SOURCES})
target_include_directories(cliviz_shared PUBLIC include/cliviz include)
set_target_properties(cliviz_shared PROPERTIES OUTPUT_NAME cliviz)

# ── Demo example (skip when building Python wheel) ──

if(NOT SKBUILD)
    add_executable(cliviz_demo examples/demo.cpp)
    target_link_libraries(cliviz_demo PRIVATE cliviz_static)
endif()

# ── Tests (optional — not available when building via pip/uv) ──

find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()
    add_subdirectory(tests)
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
install(TARGETS cliviz_static cliviz_shared
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/cliviz/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cliviz)
install(FILES include/cliviz.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
