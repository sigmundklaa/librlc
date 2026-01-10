
target_sources(
    rlc
    PRIVATE
        timer.c
        init.c
)

target_include_directories(
    rlc
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../src
)
