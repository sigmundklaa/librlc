
#ifndef RLC_RECORD_H__
#define RLC_RECORD_H__

#include <stdint.h>

#include <rlc/decl.h>
#include <rlc/list.h>
#include <rlc/errno.h>

RLC_BEGIN_DECL

struct rlc_record_node {
        struct rlc_record {
                uint32_t start;
                uint32_t end;
        } rec;

        rlc_list_node node;
};

RLC_END_DECL

#endif /* RLC_RECORD_H__ */
