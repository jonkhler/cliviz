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

# ── Demo example ──

add_executable(cliviz_demo examples/demo.cpp)
target_link_libraries(cliviz_demo PRIVATE cliviz_static)

# ── Tests ──

find_package(GTest REQUIRED)
enable_testing()
add_subdirectory(tests)

# ── Install ──

include(GNUInstallDirs)
install(TARGETS cliviz_static cliviz_shared
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/cliviz/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cliviz)
install(FILES include/cliviz.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
