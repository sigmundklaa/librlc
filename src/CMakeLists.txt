
target_sources(
    rlc
    PUBLIC
        rlc.c
        chunk.c
        encode.c
)

add_subdirectory(plat)

target_link_libraries(rlc INTERFACE librlc-linux)
