
add_custom_target(tests)

find_package(unity REQUIRED)

function(remove_sources target)
    set_source_files_properties(
        ${ARGN}
        TARGET_DIRECTORY
            ${target}
        PROPERTIES
            HEADER_FILE_ONLY ON
    )
endfunction()


function(add_test src_file)
    string(REPLACE . _ test_name_suffix ${src_file})
    set(test_name test-${test_name_suffix})

    cmake_parse_arguments(ADD_TEST "" "" "REPLACE" ${ARGN})

    add_executable(${test_name} ${src_file})
    target_link_libraries(
        ${test_name}
        PUBLIC
            rlc
            unity::framework
    )
    target_include_directories(
        ${test_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )

    if(ADD_TEST_REPLACE)
        list(
            TRANSFORM
                ADD_TEST_REPLACE
            PREPEND
                ${CMAKE_CURRENT_SOURCE_DIR}/../src/
        )
        remove_sources(${test_name} ${ADD_TEST_REPLACE})
    endif()

    set_property(
        TARGET
            tests
        APPEND
        PROPERTY
            _exes
        ${test_name}
    )
endfunction()

add_custom_target(
    run-test
    COMMAND
        /usr/bin/env bash -c
        "$<LIST:JOIN,$<LIST:TRANSFORM,$<TARGET_PROPERTY:tests,_exes>,PREPEND,./>,; >"
        || exit 0
    DEPENDS
        "$<TARGET_PROPERTY:tests,_exes>"
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)

add_test(test_encode.c)
add_test(test_sdu.c)
add_test(test_rx.c REPLACE rx.c)
add_test(test_arq.c)
