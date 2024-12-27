
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <rlc/chunks.h>

#include "encode.h"
#include "utils.h"

#define SO_WIDTH_ (16)
#define SO_SIZE_  (SO_WIDTH_ / 8)

enum seg_info {
        SEG_ALL = 0b00,
        SEG_FIRST = 0b01,
        SEG_LAST = 0b10,
        SEG_MID = 0b11,
};

static size_t sn_num_bits_(enum rlc_sn_width width)
{
        switch (width) {
        case RLC_SN_6BIT:
                return 6;
        case RLC_SN_12BIT:
                return 12;
        case RLC_SN_18BIT:
                return 18;
        default:
                assert(0);
                return 0;
        }
}

/**
 * @brief Get the number of bytes the SN part of the header will occpy.
 *
 * This is always the size of the first section of an AM/UM PDU
 */
static size_t sn_num_bytes_(enum rlc_sn_width width)
{
        size_t bits;

        bits = sn_num_bits_(width);
        return bits / 8 + ((bits % 8) != 0);
}

static uint8_t bit_copy_(uint8_t dst, uint8_t src, unsigned int idx,
                         unsigned int width)
{
        return dst | ((src & ((1 << width) - 1)) << ((8 - width) - idx));
}

static void bit_copy_mem_(uint8_t *dst, uint32_t src, unsigned int offset,
                          unsigned int width)
{
        unsigned int i;
        unsigned int bytes;
        unsigned int num_bits;
        unsigned int width_remain;
        uint8_t src_byte;

        bytes = (offset + width) / 8 + 1;

        for (i = 0; i < bytes; i++, dst++) {
                if (offset >= 8) {
                        offset -= 8;
                        continue;
                }

                num_bits = 8 - offset;
                num_bits = rlc_min(width, num_bits);

                src_byte = (src >> (width - num_bits)) & ((1 << num_bits) - 1);

                *dst = bit_copy_(*dst, src_byte, offset, num_bits);

                width -= num_bits;
                if (width == 0) {
                        break;
                }

                offset = 0;
        }
}

static enum seg_info to_si_(const struct rlc_pdu *pdu)
{
        if (pdu->flags.is_last && pdu->flags.is_first) {
                return SEG_ALL;
        }
        if (pdu->flags.is_last) {
                return SEG_LAST;
        }
        if (pdu->flags.is_first) {
                return SEG_FIRST;
        }

        return SEG_MID;
}

static void from_si_(struct rlc_pdu *pdu, enum seg_info si)
{
        if (si == SEG_ALL || si == SEG_FIRST) {
                pdu->flags.is_first = 1;
        }
        if (si == SEG_ALL || si == SEG_LAST) {
                pdu->flags.is_last = 1;
        }
}

static bool has_sn_(const struct rlc_pdu *pdu)
{
        return !(pdu->flags.is_last && pdu->flags.is_first);
}

static bool has_so_(const struct rlc_pdu *pdu)
{
        return !pdu->flags.is_first;
}

void rlc_pdu_encode(const struct rlc_context *ctx, const struct rlc_pdu *pdu,
                    struct rlc_chunk *dst)
{
        size_t size;
        size_t full_width;
        uint8_t si;

        if (pdu->type == RLC_TM) {
                return;
        }

        full_width = 0;
        size = 0;

        if (pdu->type == RLC_AM) {
                /* Data bit and polled bit */
                bit_copy_mem_(dst->data, 0b1 | (pdu->flags.polled << 1), 0, 2);

                full_width += 2;
        }

        si = to_si_(pdu);

        bit_copy_mem_(dst->data, si, full_width, 2);
        full_width += 2;

        /* Reserve necessary amount bits so that the end of the SN is aligned at
         * the end of a byte */
        if ((pdu->type == RLC_UM && ctx->sn_width == RLC_SN_12BIT) ||
            (pdu->type == RLC_AM && ctx->sn_width == RLC_SN_18BIT)) {
                full_width += 2;
        }

        if (has_sn_(pdu)) {
                bit_copy_mem_(dst->data, pdu->sn, full_width,
                              sn_num_bits_(ctx->sn_width));
                full_width += sn_num_bits_(ctx->sn_width);

                if (has_so_(pdu)) {
                        bit_copy_mem_(dst->data, pdu->seg_offset, full_width,
                                      SO_WIDTH_);
                        full_width += SO_WIDTH_;
                }
        }

        dst->size = full_width / 8 + ((full_width % 8) != 0);
}

rlc_errno rlc_pdu_decode(const struct rlc_context *ctx, struct rlc_pdu *pdu,
                         const struct rlc_chunk *chunks, size_t num_chunks)
{
        rlc_errno status;
        ssize_t size;
        size_t sn_size;
        const struct rlc_chunk *cur;
        uint8_t header[5];

        if (pdu->type == RLC_TM) {
                return 0;
        }

        (void)memset(&pdu->flags, 0, sizeof(pdu->flags));

        sn_size = sn_num_bytes_(ctx->sn_width);

        size = rlc_chunks_deepcopy(chunks, num_chunks, header, sizeof(header));
        if (size < sn_size) {
                if (size >= 0) {
                        return -ENODATA;
                }

                return size;
        }

        if (pdu->type == RLC_AM) {
                /* D/C bit is ignored, as it is assumed that anything passed
                 * to this function is a data PDU */
                pdu->flags.polled = (header[0] >> 6) & 1;

                from_si_(pdu, (header[0] >> 4) & 0x3);

                if (has_sn_(pdu)) {
                        if (ctx->sn_width == RLC_SN_12BIT) {
                                pdu->sn =
                                        ((header[0] & 0xf) << 8) | (header[1]);
                        } else {
                                pdu->sn = ((header[0] & 0x3) << 16) |
                                          (header[1] << 8) | header[2];
                        }
                }
        } else {
                from_si_(pdu, (header[0] >> 6) & 0x3);

                if (has_sn_(pdu)) {
                        if (ctx->sn_width == RLC_SN_6BIT) {
                                pdu->sn = header[0] & 0x3f;
                        } else {
                                pdu->sn =
                                        ((header[0] & 0xf) << 8) | (header[1]);
                        }
                }
        }

        if (has_so_(pdu)) {
                if (size < (sn_size + 1)) {
                        return -ENODATA;
                }

                pdu->seg_offset =
                        (header[sn_size] << 8) | (header[sn_size + 1]);
        } else {
                pdu->seg_offset = 0;
        }

        return 0;
}

size_t rlc_pdu_header_size(const struct rlc_context *ctx,
                           const struct rlc_pdu *pdu)
{
        switch (pdu->type) {
        case RLC_AM:
        case RLC_UM:
                return sn_num_bytes_(ctx->sn_width) + (SO_SIZE_ * has_so_(pdu));
        case RLC_TM:
                return 0;
        default:
                assert(0);
                return 0;
        }
}
