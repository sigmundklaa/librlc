
zephyr_library()
zephyr_interface_library_named(rlc_iface)

gabs_require(gabs-mutex gabs-semaphore gabs-log gabs-pbuf gabs-timer)

target_include_directories(
    rlc_iface
    INTERFACE
        ${ZEPHYR_CURRENT_MODULE_DIR}/include
)
target_link_libraries(rlc_iface INTERFACE gabs_iface)

zephyr_library_link_libraries(rlc_iface)

zephyr_library_sources(
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/rlc.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/arq.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/backend.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/encode.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/tx.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/rx.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/event.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/log.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/sdu.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/timer.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/sched.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/seg_buf.c
    ${ZEPHYR_CURRENT_MODULE_DIR}/src/seg_list.c
)
