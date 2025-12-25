cmake_minimum_required(VERSION 3.20)
project(rlc)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT RLC_GNB_REPOSITORY)
    set(RLC_GNB_REPOSITORY git@github.com:sigmundklaa/libgnb.git)
endif()

if(NOT RLC_GNB_TAG)
    set(RLC_GNB_TAG main)
endif()

option(RLC_LINUX "Compile for Linux" ON)

add_library(rlc)
include(FetchContent)

target_include_directories(rlc PUBLIC include)

FetchContent_Declare(
    gnb-git
    GIT_REPOSITORY ${RLC_GNB_REPOSITORY}
    GIT_TAG ${RLC_GNB_TAG}
)
FetchContent_MakeAvailable(gnb-git)

target_link_libraries(rlc PUBLIC gnb)

add_subdirectory(src)

if(TEST)
    add_subdirectory(tests)
endif()

if(TEST_PLATFORM)
    add_subdirectory(tests/platform)
endif()

if(RLC_LINUX)
    add_subdirectory(linux)
endif()

unset(CACHE{RLC_GNB_REPOSITORY})
unset(CACHE{RLC_GNB_TAG})
