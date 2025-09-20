
rlc_plat_define(
    rlc-zephyr
    SOURCES
        init.c
        lock.c
        timer.c
        buf.c
)

target_link_libraries(rlc-zephyr INTERFACE zephyr_interface) 
