
add_custom_target(tests)

find_package(unity REQUIRED)

function(add_test src_file)
    string(REPLACE . _ test_name_suffix ${src_file})
    set(test_name test-${test_name_suffix})

    add_executable(${test_name} ${src_file})
    target_link_libraries(
        ${test_name}
        PRIVATE
            librlc
            unity
    )
    target_include_directories(
        ${test_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )

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

add_test(test_chunks.c)
add_test(test_encode.c)
