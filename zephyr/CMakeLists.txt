
add_subdirectory(../ rlc)
rlc_plat_use(rlc-zephyr)

zephyr_interface_library_named(zrlc)
target_link_libraries(zrlc INTERFACE rlc)
