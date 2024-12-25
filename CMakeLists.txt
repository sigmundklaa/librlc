cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(librlc)
add_library(librlc STATIC)

target_include_directories(librlc PUBLIC include)

add_subdirectory(src)
add_subdirectory(tests)

add_executable(rlc main.c)
target_link_libraries(rlc PRIVATE librlc)
