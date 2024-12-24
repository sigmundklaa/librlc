
#include <stdint.h>

#define rlc_max(a, b)   (((a) > (b)) ? (a) : (b))
#define rlc_min(a, b)   (((a) < (b)) ? (a) : (b))
#define rlc_assert(...) assert(__VA_ARGS__)

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#define rlc_errf(fmt_, ...) (void)fprintf(stderr, fmt_, ##__VA_ARGS__)

static inline const void *rlc_voidptr_offset(const void *base, size_t offset)
{
        return (const void *)((uint8_t *)base + offset);
}
