
cmake_minimum_required(VERSION 3.20)
project(ip_sample)

add_executable(ip_sample main.c)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ rlc)
target_link_libraries(ip_sample PRIVATE rlc)
