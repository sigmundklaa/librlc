cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr)
project(app)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../ rlc)

target_sources(app PRIVATE main.c radio.c mem.c)
target_link_libraries(app PRIVATE rlc)

rlc_plat_use(rlc-zephyr)
