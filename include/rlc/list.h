
#ifndef RLC_LIST_H__
#define RLC_LIST_H__

#include <stddef.h>

#include <rlc/decl.h>

RLC_BEGIN_DECL

#define rlc_container_of(ptr_, type_, mem_)                                    \
        ((type_ *)(((uintptr_t)ptr_) + (uintptr_t)&((type_ *)NULL)->mem_))

#define rlc_typeof__(x) typeof(x)

typedef struct rlc_list_node {
        struct rlc_list_node *next;
} rlc_list_node;

typedef struct rlc_list {
        struct rlc_list_node *head;
} rlc_list;

#define rlc_concat3__(x, y, z) x##y##z
#define rlc_el__(name_)        rlc_concat3__(rlc_el_, name_, __LINE__)

/**
 * @brief Iterate over each node in list
 *
 */
#define rlc_each_list(list_, curptr_, list_member_)                            \
        struct rlc_list_node *rlc_el__(cur) = list_->head;                     \
        rlc_el__(cur) != NULL &&                                               \
                (curptr_ = rlc_container_of(rlc_el__(cur),                     \
                                            rlc_typeof__(*curptr_),            \
                                            list_member_));                    \
        rlc_el__(cur) = rlc_el__(cur)->next

/**
 * @brief Iterate over each node in a list, ensuring that it is still
 * safe to iterate even if the current node is removed from the list during
 * iteration.
 *
 * @param list_ List head
 * @param curptr_ Target pointer; pointer holding the current item
 * @param list_mem_ List node member name within container
 */
#define rlc_each_list_safe(list_, curptr_, list_member_)                       \
        struct rlc_list_node *rlc_el__(                                        \
                cur) = (list_)->head,                                          \
      *rlc_el__(next) = ((list_)->head == NULL ? NULL : (list_)->head->next);  \
        rlc_el__(cur) != NULL &&                                               \
                (curptr_ = rlc_container_of(rlc_el__(cur),                     \
                                            rlc_typeof__(*curptr_),            \
                                            list_member_));                    \
        rlc_el__(cur) = rlc_el__(next),                                        \
        rlc_el__(next) = rlc_el__(next) == NULL ? NULL : rlc_el__(next)->next

#define rlc_list_init(head_) ((struct rlc_list){.head = head_})

#define rlc_list_head_node(list_) ((list_)->head)

#define rlc_list_head(list_, type_, list_member_)                              \
        rlc_container_of(rlc_list_head_node(list_), type_, list_member_)

#define rlc_list_next_node(container_, list_member_)                           \
        (container_->list_member_.next)

#define rlc_list_next(container_, list_member_)                                \
        rlc_container_of(rlc_list_next_node(container_, list_member_),         \
                         rlc_typeof__(*container_), list_member_)

#define rlc_list_put_node(slotptr_, node_)                                     \
        do {                                                                   \
                node_->next = *slotptr_;                                       \
                *slotptr_ = node_;                                             \
        } while (0)
#define rlc_list_put(slotptr_, cur_, list_member_)                             \
        rlc_list_put_node(slotptr_, (&cur_->list_member_))

#define rlc_list_pop_node(slotptr_, node_)                                     \
        do {                                                                   \
                *slotptr_ = node_->next;                                       \
                node_->next = NULL;                                            \
        } while (0)
#define rlc_list_pop(slotptr_, cur_, list_member_)                             \
        rlc_list_pop_node(slotptr_, (&cur_->list_member_))

RLC_END_DECL

#endif /* RLC_LIST_H__ */
