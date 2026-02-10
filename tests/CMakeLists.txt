cmake_minimum_required(VERSION 3.20)
project(rlc-test)

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
        test_rx.cc
)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_subdirectory(../ rlc)

# Remove source file
macro(remove_sources)
    set_source_files_properties(
        ${ARGN}
        TARGET_DIRECTORY
            rlc
        PROPERTIES
            HEADER_FILE_ONLY ON
    )
endmacro()

# Included in test_rx.cc
remove_sources(rx.c)

target_link_libraries(tests PRIVATE Catch2::Catch2WithMain rlc gabs)
