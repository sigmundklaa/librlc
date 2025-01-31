
cmake_minimum_required(VERSION 3.20)
project(simple_example)

add_executable(simple simple.c)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ rlc)
target_link_libraries(simple PRIVATE rlc)
