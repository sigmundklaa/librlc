
#ifndef RLC_WINDOW_H__
#define RLC_WINDOW_H__

#include <stdint.h>
#include <stdbool.h>

#include <rlc/utils.h>

RLC_BEGIN_DECL

struct rlc_window {
        uint32_t base;
        uint32_t width;
};

static inline void rlc_window_init(struct rlc_window *win, uint32_t base,
                                   uint32_t width)
{
        win->base = base;
        win->width = width;
}

static inline bool rlc_window_has(struct rlc_window *win, uint32_t num)
{
        return num >= win->base && num < win->base + win->width;
}

static inline void rlc_window_move(struct rlc_window *win, uint32_t distance)
{
        win->base += distance;
}

static inline void rlc_window_move_to(struct rlc_window *win, uint32_t pos)
{
        win->base = pos;
}

static inline uint32_t rlc_window_index(struct rlc_window *win, uint32_t pos)
{
        return pos - win->base;
}

static inline uint32_t rlc_window_base(struct rlc_window *win)
{
        return win->base;
}

static inline uint32_t rlc_window_end(struct rlc_window *win)
{
        return win->base + win->width;
}

RLC_END_DECL

#endif /* RLC_WINDOW_H__ */
