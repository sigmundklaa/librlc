
cmake_minimum_required(VERSION 3.20)
project(cxx_sample LANGUAGES C CXX)

add_executable(cxx_sample main.cxx)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ rlc)
target_link_libraries(cxx_sample PRIVATE rlc)

rlc_plat_use(rlc-linux)
