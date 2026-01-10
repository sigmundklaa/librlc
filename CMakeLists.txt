cmake_minimum_required(VERSION 3.20)
project(rlc)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT GABS_LIBRARY)
    message(FATAL_ERROR "Gabs not found")
endif()

option(RLC_LINUX "Compile for Linux" ON)

add_library(rlc)

target_include_directories(rlc PUBLIC include)
target_link_libraries(rlc PUBLIC ${GABS_LIBRARY})

gabs_require(gabs-mutex gabs-semaphore gabs-log gabs-pbuf)

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
