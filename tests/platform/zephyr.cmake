
cmake_minimum_required(VERSION 3.20)


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ rlc)
rlc_plat_use(rlc-zephyr)

target_sources(app PRIVATE test_buf.c)
target_link_libraries(app PRIVATE rlc)
