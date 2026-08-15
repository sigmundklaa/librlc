cmake_minimum_required(VERSION 3.20)
project(rlc-test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RLC_COVERAGE "Instrument the library for gcovr" OFF)

include(CTest)
include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1 # or a later release
)
FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(
    gabs
    GIT_REPOSITORY https://github.com/sigmundklaa/gabs.git
    GIT_TAG main
)
FetchContent_MakeAvailable(gabs)

add_executable(tests)
target_sources(
    tests
    PRIVATE
        test_list.cc
        test_seg_buf.cc
        test_seg_list.cc
        test_am.cc
        test_um.cc
        test_tm.cc
        test_encode.cc
        test_tx.cc
        test_rx.cc
        test_arq.cc
)
target_include_directories(
    tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(../ rlc)
add_subdirectory(gabs-overrides)

gabs_select(gabs-timer-rlc-tests)

# Exclude a source file from the rlc library so it can be #included directly
# in a test file to expose its static functions.
macro(remove_sources)
    set_source_files_properties(
        ${ARGN}
        TARGET_DIRECTORY
            rlc
        PROPERTIES
            HEADER_FILE_ONLY ON
    )
endmacro()

remove_sources(${CMAKE_CURRENT_SOURCE_DIR}/../src/tx.c)
remove_sources(${CMAKE_CURRENT_SOURCE_DIR}/../src/rx.c)
remove_sources(${CMAKE_CURRENT_SOURCE_DIR}/../src/arq.c)

target_link_libraries(tests PRIVATE Catch2::Catch2WithMain rlc gabs)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)

# One ctest entry per TEST_CASE, so a failure names the case and -R can
# select a subset.
catch_discover_tests(tests)

if(RLC_COVERAGE)
    # The tests #include tx.c, rx.c and arq.c to reach their statics, so the
    # counters for those live in the test objects rather than in rlc.
    foreach(target rlc tests)
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        target_link_options(${target} PRIVATE --coverage)
    endforeach()

    find_program(GCOVR gcovr REQUIRED)

    # gcovr matches --filter against the paths as written, so normalize each
    # one: neither the tests/.. nor the // from joining survives a match.
    cmake_path(SET RLC_ROOT NORMALIZE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    cmake_path(SET RLC_SRC NORMALIZE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    cmake_path(SET RLC_INCLUDE NORMALIZE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

    # --delete clears the counters on the way out, so each run reports what
    # this run covered rather than accumulating across runs.
    add_custom_target(
        coverage
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMAND
            ${GCOVR} ${CMAKE_BINARY_DIR}
            --root ${RLC_ROOT}
            --filter ${RLC_SRC}
            --filter ${RLC_INCLUDE}
            --txt --print-summary --delete
        DEPENDS tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL
        VERBATIM
    )
endif()
