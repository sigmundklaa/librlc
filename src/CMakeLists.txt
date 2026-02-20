
target_sources(
    rlc
    PRIVATE
        rlc.c
        log.c
        encode.c
        event.c
        sdu.c
        seg_buf.c
        seg_list.c
        timer.c
        arq.c
        rx.c
        tx.c
        backend.c
        sched.c
)
