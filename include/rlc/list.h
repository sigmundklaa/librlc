
#ifndef RLC_LIST_H__
#define RLC_LIST_H__

#include <stddef.h>
#include <stdbool.h>

#include <rlc/decl.h>
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

#define rlc_list_it_safe__(name_) rlc_list_it_##name_##__LINE__##_safe__

/**
 * @brief Iterate over @p list_, keeping the iterator in @p it_. It is safe to
 * remove using the iterator @p it_
 *
 * This expression is quite ugly, so here goes an explanation:
 * The for loop is, as any other for loop, split into three parts:
 * initialization, conitional check and continuation. Since we want the
 * iterator to be outside the scope of the loop, for example for inserting at
 * the end, we need to use the initialization to declare the next iterator.
 * However, since we declare the variable here we can't also assign to @p it_
 * as we would normally. So, we make use of the not so well known comma
 * operator, which works as below:
 * int a = (b, c); // evaluates to a = c
 *
 * The conditional check simply checks if we are at end of iteration. The
 * continuation step stores the previously loaded next iterator into @p it_,
 * and loads the next value now, instead of at the end of the next iteration.
 *
 * This behaviour makes it so  that we don't rely on the current value to
 * get the next, so the current value can be removed and/or deallocated while
 * still continuing to iterate safely.
 */
#define rlc_list_foreach_safe(list_, it_)                                      \
        for (struct rlc_list_it rlc_list_it_safe__(next) =                     \
                     (it_ = rlc_list_it_init(list_), rlc_list_it_next(it_));   \
             !rlc_list_it_eoi(it_); it_ = rlc_list_it_safe__(next),            \
                                rlc_list_it_safe__(next) = rlc_list_it_next(   \
                                        rlc_list_it_safe__(next)))

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

static inline rlc_list_node *rlc_list_it_node(rlc_list_it it)
{
        return it.node;
}

#define rlc_list_it_item(it_, struct_, member_)                                \
        gabs_container_of(rlc_list_it_node(it_), struct_, member_)

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
        it.skip_iter = true;

        return it;
}

RLC_END_DECL

#endif /* RLC_LIST_H__ */
