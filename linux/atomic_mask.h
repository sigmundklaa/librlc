
#ifndef RLC_LINUX_ATOMIC_MASK_H__
#define RLC_LINUX_ATOMIC_MASK_H__

#include <rlc/utils.h>
#include <rlc_plat.h>

RLC_BEGIN_DECL

static inline void mask_set(atomic_uint *state, unsigned int mask)
{
        unsigned int masked;
        unsigned int expected;

        masked = mask;
        expected = 0;

        while (!atomic_compare_exchange_weak(state, &expected, masked)) {
                masked = expected | mask;
        }
}

/** @brief Set mask, only if previously unset */
static inline bool mask_set_strict(atomic_uint *state, unsigned int mask)
{
        unsigned int masked;
        unsigned int expected;

        expected = 0;
        masked = mask;

        while (!atomic_compare_exchange_weak(state, &expected, masked)) {
                if ((expected & mask) != 0) {
                        return false;
                }
        }

        return true;
}

static inline void mask_clear(atomic_uint *state, unsigned int mask)
{
        unsigned int masked;
        unsigned int expected;

        masked = 0;
        expected = 0;

        while (!atomic_compare_exchange_weak(state, &expected, masked)) {
                masked = expected & (~mask);
        }
}

static inline bool mask_cas(atomic_uint *state, unsigned int cmp,
                            unsigned int set)
{
        unsigned int masked;
        unsigned int expected;

        expected = cmp;
        masked = set;

        while (!atomic_compare_exchange_weak(state, &expected, masked)) {
                if ((expected & cmp) != cmp) {
                        return false;
                }

                masked = expected | set;
        }

        return true;
}

static inline unsigned int mask_get(atomic_uint *state, unsigned int mask)
{
        return atomic_load(state) & mask;
}

static inline bool mask_test(atomic_uint *state, unsigned int mask)
{
        return mask_get(state, mask) == mask;
}

RLC_END_DECL

#endif /* RLC_LINUX_ATOMIC_MASK_H__ */
