
#ifndef MEM_H__
#define MEM_H__

#include <zephyr/kernel.h>

#include <rlc/rlc.h>

#ifdef __cplusplus
extern "C" {
#endif

void *l2_mem_alloc(rlc_context *ctx, size_t size, enum rlc_alloc_type type);
void l2_mem_dealloc(rlc_context *ctx, void *mem, enum rlc_alloc_type type);

#ifdef __cplusplus
}
#endif

#endif /* MEM_H__ */
