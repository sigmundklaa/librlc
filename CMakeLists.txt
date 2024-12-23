cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(librlc)
add_executable(librlc main.c)

add_subdirectory(src)

target_include_directories(librlc
    PRIVATE
        src
    PUBLIC
        include
)
