
#ifndef RLC_CHUNKS_H__
#define RLC_CHUNKS_H__

#include <rlc/rlc.h>

size_t rlc_chunks_size(const struct rlc_chunk *chunks, size_t len);

size_t rlc_chunks_num_view(const struct rlc_chunk *chunks, size_t num_chunks,
                           size_t size, size_t offset);

size_t rlc_chunks_num(const struct rlc_chunk *chunks, size_t num_chunks,
                      size_t size);

ssize_t rlc_chunks_deepcopy_view(const struct rlc_chunk *chunks,
                                 size_t num_chunks, void *dst, size_t max_size,
                                 size_t offset);

ssize_t rlc_chunks_deepcopy(const struct rlc_chunk *chunks, size_t num_chunks,
                            void *dst, size_t max_size);

ssize_t rlc_chunks_copy_view(const struct rlc_chunk *chunks, size_t num_chunks,
                             struct rlc_chunk *dst_chunks, size_t max_size,
                             size_t offset);

ssize_t rlc_chunks_copy(const struct rlc_chunk *chunks, size_t num_chunks,
                        struct rlc_chunk *dst_chunks, size_t max_size);

#endif /* RLC_CHUNKS_H__ */
