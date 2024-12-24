
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include <rlc/rlc.h>
#include <rlc/chunks.h>

#include "utils.h"

size_t rlc_chunks_size(const struct rlc_chunk *chunks, size_t len)
{
        const struct rlc_chunk *cur;
        size_t total;

        total = 0;

        for (rlc_each_item(chunks, cur, len)) {
                total += cur->size;
        }

        return total;
}

size_t rlc_chunks_num_view(const struct rlc_chunk *chunks, size_t num_chunks,
                           size_t size, size_t offset)
{
        const struct rlc_chunk *cur;
        size_t total;
        size_t passed;
        size_t count;
        size_t tmp;

        total = 0;
        passed = 0;
        count = 0;

        for (rlc_each_item(chunks, cur, num_chunks)) {
                if (passed >= offset) {
                        tmp = rlc_min(cur->size, size - total);
                        total += tmp;
                        passed += tmp;
                } else if (passed + cur->size >= offset) {
                        tmp = rlc_min(cur->size - (offset - passed),
                                      size - total);
                        total += tmp;
                        passed += tmp;
                } else {
                        passed += cur->size;
                        continue;
                }

                count++;

                if (total >= size) {
                        break;
                }
        }

        return count;
}

size_t rlc_chunks_num(const struct rlc_chunk *chunks, size_t num_chunks,
                      size_t size)
{
        return rlc_chunks_num_view(chunks, num_chunks, size, 0);
}

ssize_t rlc_chunks_deepcopy_view(const struct rlc_chunk *chunks,
                                 size_t num_chunks, void *dst, size_t max_size,
                                 size_t offset)
{
        const struct rlc_chunk *cur;
        size_t total;
        size_t copy_size;
        size_t passed;
        size_t local_offset;

        total = 0;
        passed = 0;

        for (rlc_each_item(chunks, cur, num_chunks)) {
                if (passed >= offset) {
                        /* The offset is either aligned at the chunk, or the
                         * unaligned offset has already been handled in the
                         * case below. */
                        copy_size = rlc_min(cur->size, max_size - total);

                        (void)memcpy((uint8_t *)dst + total, cur->data,
                                     copy_size);

                        total += copy_size;
                        passed += copy_size;
                } else if (passed + cur->size >= offset) {
                        /* Offset into a chunk */
                        local_offset = offset - passed;
                        copy_size = rlc_min(cur->size - local_offset,
                                            max_size - total);

                        (void)memcpy((uint8_t *)dst + total,
                                     (uint8_t *)cur->data + local_offset,
                                     copy_size);

                        total += copy_size;
                        passed += copy_size;
                } else {
                        passed += cur->size;
                        continue;
                }

                if (total >= max_size) {
                        break;
                }
        }

        return total;
}

ssize_t rlc_chunks_deepcopy(const struct rlc_chunk *chunks, size_t num_chunks,
                            void *dst, size_t max_size)
{
        return rlc_chunks_deepcopy_view(chunks, num_chunks, dst, max_size, 0);
}

ssize_t rlc_chunks_copy_view(const struct rlc_chunk *chunks, size_t num_chunks,
                             struct rlc_chunk *dst_chunks, size_t max_size,
                             size_t offset)
{
        const struct rlc_chunk *cur;
        size_t total;
        size_t copy_size;
        size_t passed;
        size_t local_off;
        size_t i;

        total = 0;
        passed = 0;
        i = 0;

        for (rlc_each_item(chunks, cur, num_chunks)) {
                if (passed >= offset) {
                        /* The offset is either aligned at the chunk, or the
                         * unaligned offset has already been handled in the
                         * case below. */
                        copy_size = rlc_min(cur->size, max_size - total);

                        dst_chunks[i].data = cur->data;
                        dst_chunks[i].size = copy_size;

                        total += copy_size;
                        passed += copy_size;
                } else if (passed + cur->size >= offset) {
                        /* Offset into a chunk */
                        local_off = offset - passed;
                        copy_size = rlc_min(cur->size - local_off,
                                            max_size - total);

                        dst_chunks[i].data = (uint8_t *)cur->data + local_off;
                        dst_chunks[i].size = copy_size;

                        total += copy_size;
                        passed += copy_size;
                } else {
                        passed += cur->size;
                        continue;
                }

                if (total >= max_size) {
                        break;
                }

                i++;
        }

        return total;
}

ssize_t rlc_chunks_copy(const struct rlc_chunk *chunks, size_t num_chunks,
                        struct rlc_chunk *dst_chunks, size_t max_size)
{
        return rlc_chunks_copy_view(chunks, num_chunks, dst_chunks, max_size,
                                    0);
}
