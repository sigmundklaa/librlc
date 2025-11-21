
rlc_plat_define(
    rlc-zephyr
    SOURCES
        init.c
        sync.c
        timer.c
        log.c
)

target_link_libraries(rlc-zephyr INTERFACE zephyr_interface) 
