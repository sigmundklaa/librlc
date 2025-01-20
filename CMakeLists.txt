cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(rlc)
add_library(rlc STATIC)

target_include_directories(rlc PUBLIC include)

add_subdirectory(src)

if(TEST)
    add_subdirectory(tests)
endif()

add_executable(rlcexe main.c)
target_link_libraries(rlcexe PRIVATE rlc)
