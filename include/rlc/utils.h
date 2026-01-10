
#ifndef RLC_UTILS_H__
#define RLC_UTILS_H__

#include <assert.h>

#include <rlc/decl.h>

RLC_BEGIN_DECL

#define rlc_max(a, b)   (((a) > (b)) ? (a) : (b))
#define rlc_min(a, b)   (((a) < (b)) ? (a) : (b))
#define rlc_assert(...) assert(__VA_ARGS__)

#define rlc_mod(x) x

#define rlc_panicf(status_, fmt_, ...)                                         \
        do {                                                                   \
                rlc_assert(0);                                                 \
        } while (0)

#define rlc_array_size(x) (sizeof(x) / sizeof((x)[0]))

#define rlc_each_node(start_, tptr_, prop_name_)                               \
        tptr_ = start_;                                                        \
        tptr_ != NULL;                                                         \
        tptr_ = tptr_->prop_name_

#define rlc_concat3__(x, y, z) x##y##z
#define rlc_ens_safe__(name_)  rlc_concat3__(rlc_ens_, name_, __LINE__)

/**
 * @brief Iterate over each node in a linked list, ensuring that it is still
 * safe to iterate even if the current node is removed from the list during
 * iteration.
 *
 * To do this two temporary variables are used during the iteration, which
 * requires the caller to pass @p type_ in as an argument
 *
 * @param type_ Type of item being iterated over
 * @param start_ Head of linked list
 * @param tptr_ Target pointer; pointer holding the current item
 * @param prop_name_ Property name of the next entry in @p type_
 */
#define rlc_each_node_safe(type_, start_, tptr_, prop_name_)                   \
        type_ *rlc_ens_safe__(cur__) = start_,                                 \
              *rlc_ens_safe__(next__) =                                        \
                      start_ == NULL ? NULL : start_->prop_name_;              \
        (tptr_ = rlc_ens_safe__(cur__)) != NULL;                               \
        rlc_ens_safe__(cur__) = rlc_ens_safe__(next__),                        \
        rlc_ens_safe__(next__) = rlc_ens_safe__(next__) == NULL                \
                                         ? NULL                                \
                                         : rlc_ens_safe__(next__)->prop_name_

#define rlc_each_item(arr_, cur_)                                              \
        cur_ = &arr_[0];                                                       \
        cur_ < &arr_[rlc_array_size(arr_)];                                    \
        cur_++

RLC_END_DECL

#endif /* RLC_UTILS_H__ */
