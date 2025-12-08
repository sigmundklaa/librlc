cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(RLC_LINUX "Compile for Linux" ON)

project(rlc)
add_library(rlc)

target_include_directories(rlc PUBLIC include)

add_subdirectory(src)

add_subdirectory(../libgnb gnb)
target_link_libraries(rlc PUBLIC gnb)

if(TEST)
    add_subdirectory(tests)
endif()

if(TEST_PLATFORM)
    add_subdirectory(tests/platform)
endif()

if(RLC_LINUX)
    add_subdirectory(linux)
endif()
