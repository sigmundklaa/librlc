cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(rlc)
add_library(rlc STATIC)

target_include_directories(rlc PUBLIC include)

add_subdirectory(src)

if(TEST)
    add_subdirectory(tests)
endif()

macro(rlc_plat_define name)
    set(kwargs "SOURCES")
    cmake_parse_arguments(RLC_PLAT_DEFINE "" "" "${kwargs}" ${ARGN})

    add_library(${name} INTERFACE)

    if(RLC_PLAT_DEFINE_SOURCES)
        target_sources(${name} INTERFACE ${RLC_PLAT_DEFINE_SOURCES})
    endif()

    target_include_directories(${name}
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
    )
endmacro()

function(rlc_plat_use name)
    target_link_libraries(rlc PUBLIC ${name})
endfunction()

add_subdirectory(platform)

rlc_plat_use(rlc-linux)
