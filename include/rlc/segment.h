
#ifndef RLC_SEGMENT_H__
#define RLC_SEGMENT_H__

#include <stdint.h>
#include <stdbool.h>

#include <rlc/decl.h>

RLC_BEGIN_DECL

struct rlc_segment {
        uint32_t start;
        uint32_t end;
};

static inline bool rlc_segment_okay(struct rlc_segment *segment)
{
        return segment->start != 0 || segment->end != 0;
}

RLC_END_DECL

#endif /*  RLC_SEGMENT_H__ */
