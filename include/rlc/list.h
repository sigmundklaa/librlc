
#ifndef RLC_LIST_H__
#define RLC_LIST_H__

#include <stddef.h>
#include <stdbool.h>

#include <rlc/utils.h>
#include <gabs/core/util.h>

RLC_BEGIN_DECL

typedef struct rlc_list_node {
        struct rlc_list_node *next;
} rlc_list_node;

typedef struct rlc_list {
        struct rlc_list_node *head;
} rlc_list;

typedef struct rlc_list_it {
        struct rlc_list_node *node;
        struct rlc_list_node **slotptr;

        bool skip_iter;
} rlc_list_it;

static inline void rlc_list_init(rlc_list *list)
{
        list->head = NULL;
}

static inline void rlc_list_node_init(rlc_list_node *node)
{
        node->next = NULL;
}

/**
 * @brief Iterate over @p list_, keeping the iterator in @p it_
 *
 * Note that @p it_ must already be declared.
 */
#define rlc_list_foreach(list_, it_)                                           \
        for (it_ = rlc_list_it_init(list_); !rlc_list_it_eoi(it_);             \
             it_ = rlc_list_it_next(it_))

static inline rlc_list_it rlc_list_it_init(rlc_list *list)
{
        return (struct rlc_list_it){
                .node = list->head,
                .slotptr = &list->head,
                .skip_iter = false,
        };
}

static inline rlc_list_it rlc_list_it_next(rlc_list_it it)
{
        if (it.node == NULL) {
                return it;
        }

        if (it.skip_iter) {
                it.skip_iter = false;
                return it;
        }

        return (struct rlc_list_it){
                .node = it.node->next,
                .slotptr = &it.node->next,
                .skip_iter = false,
        };
}

static inline rlc_list_it rlc_list_it_skip(rlc_list_it it)
{
        rlc_list_it next;

        next = rlc_list_it_next(it);
        next.slotptr = it.slotptr;

        return next;
}

static inline rlc_list_it rlc_list_it_repeat(rlc_list_it it)
{
        it.skip_iter = true;
        return it;
}

static inline rlc_list_node *rlc_list_it_node(rlc_list_it it)
{
        return it.node;
}

#define rlc_list_it_item(it_, struct_, member_)                                \
        (rlc_list_it_node(it_) == NULL)                                        \
                ? NULL                                                         \
                : gabs_container_of(rlc_list_it_node(it_), struct_, member_)

static inline bool rlc_list_it_eoi(rlc_list_it it)
{
        return rlc_list_it_node(it) == NULL;
}

/**
 * @brief Put @p node into the underlying list of @p it, placing it before
 * the item in @p it.
 *
 * */
static inline rlc_list_it rlc_list_it_put_front(rlc_list_it it,
                                                rlc_list_node *node)
{
        *it.slotptr = node;
        node->next = it.node;

        return (struct rlc_list_it){
                .node = it.node,
                .slotptr = &node->next,
                .skip_iter = false,
        };
}

static inline rlc_list_it rlc_list_it_put_back(rlc_list_it it,
                                               rlc_list_node *node)
{
        return rlc_list_it_put_front(rlc_list_it_next(it), node);
}

static inline rlc_list_it rlc_list_it_pop(rlc_list_it it, rlc_list_node **out)
{
        struct rlc_list_node *next;

        if (out != NULL) {
                *out = it.node;
        }

        next = it.node->next;
        it.node->next = NULL;

        it.node = next;
        *it.slotptr = next;

        /* Returns next iterator, so next call to it_next() should return the
         * same iterator to prevent skipping the next item. */
        return rlc_list_it_repeat(it);
}

RLC_END_DECL

#endif /* RLC_LIST_H__ */
