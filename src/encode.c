
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <rlc/rlc.h>

#include "utils.h"
#include "encode.h"
#include "log.h"

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

static unsigned int width_aligned_(unsigned int width)
{
        return (width + 7) & ~7;
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

static bool has_sn_(const struct rlc_pdu *pdu, enum rlc_sdu_type type)
{
        return (type != RLC_UM || !(pdu->flags.is_last && pdu->flags.is_first));
}

static bool has_so_(const struct rlc_pdu *pdu)
{
        return !pdu->flags.is_first && !pdu->flags.is_status;
}

static size_t bytes_ceil_(size_t num_bits)
{

        return num_bits / 8 + ((num_bits % 8) != 0);
}

static void encode_status_header_(const struct rlc_context *ctx,
                                  const struct rlc_pdu *pdu, gabs_pbuf *buf)
{
        size_t full_width;
        size_t sn_width;
        uint8_t data[RLC_STATUS_MAX_SIZE];

        (void)memset(data, 0, sizeof(data));

        full_width = 0;

        bit_copy_mem_(data, (0b0 << 1) | 0b000, 0, 4);
        full_width += 4;

        sn_width = sn_num_bits_(ctx->conf->sn_width);
        bit_copy_mem_(data, pdu->sn, full_width, sn_width);
        full_width += sn_width;

        bit_copy_mem_(data, pdu->flags.ext, full_width, 1);
        full_width += 1;

        gabs_pbuf_put(buf, data, bytes_ceil_(full_width));
}

void rlc_pdu_encode(struct rlc_context *ctx, const struct rlc_pdu *pdu,
                    gabs_pbuf *buf)
{
        size_t size;
        size_t full_width;
        uint8_t si;
        uint8_t data[RLC_PDU_HEADER_MAX_SIZE];

        (void)memset(data, 0, sizeof(data));

        switch (ctx->type) {
        case RLC_TM:
                /* Nothing to be done */
                return;
        case RLC_AM:
                if (pdu->flags.is_status) {
                        encode_status_header_(ctx, pdu, buf);
                        return;
                }
                /* fallthrough */
        case RLC_UM:
                break;
        }

        full_width = 0;
        size = 0;

        if (ctx->type == RLC_AM) {
                /* Data bit and polled bit */
                bit_copy_mem_(data, (0b1 << 1) | pdu->flags.polled, 0, 2);

                full_width += 2;
        }

        si = to_si_(pdu);

        bit_copy_mem_(data, si, full_width, 2);
        full_width += 2;

        /* Reserve necessary amount bits so that the end of the SN is aligned at
         * the end of a byte */
        if ((ctx->type == RLC_UM && ctx->conf->sn_width == RLC_SN_12BIT) ||
            (ctx->type == RLC_AM && ctx->conf->sn_width == RLC_SN_18BIT)) {
                full_width += 2;
        }

        if (has_sn_(pdu, ctx->type)) {
                bit_copy_mem_(data, pdu->sn, full_width,
                              sn_num_bits_(ctx->conf->sn_width));
                full_width += sn_num_bits_(ctx->conf->sn_width);

                if (has_so_(pdu)) {
                        bit_copy_mem_(data, pdu->seg_offset, full_width,
                                      SO_WIDTH_);
                        full_width += SO_WIDTH_;
                }
        }

        gabs_pbuf_put(buf, data, bytes_ceil_(full_width));
}

static rlc_errno
decode_status_header_(struct rlc_context *ctx, struct rlc_pdu *pdu,
                      const uint8_t header[RLC_PDU_HEADER_MAX_SIZE])
{
        uint8_t cpt;

        /* CPT is reserved and must always be zero */
        cpt = (header[0] >> 4) & 0x7;
        if (cpt != 0) {
                rlc_errf("CPT is non-zero: %d", cpt);
                return -ENOTSUP;
        }

        if (ctx->conf->sn_width == RLC_SN_12BIT) {
                pdu->sn = ((header[0] & 0xf) << 8) | (header[1]);
                pdu->flags.ext = (header[2] >> 7) & 0x1;
        } else {
                pdu->sn = ((header[0] & 0xf) << 14) | (header[1] << 6) |
                          ((header[2] >> 2) & 0x3f);
                pdu->flags.ext = (header[2] >> 1) & 0x1;
        }

        return 0;
}

rlc_errno rlc_pdu_decode(struct rlc_context *ctx, struct rlc_pdu *pdu,
                         gabs_pbuf *buf)
{
        rlc_errno status;
        ptrdiff_t size;
        size_t sn_size;
        uint8_t header[RLC_PDU_HEADER_MAX_SIZE];

        if (ctx->type == RLC_TM) {
                return 0;
        }

        status = 0;

        (void)memset(&pdu->flags, 0, sizeof(pdu->flags));

        sn_size = sn_num_bytes_(ctx->conf->sn_width);

        size = gabs_pbuf_copy(*buf, header, 0, sizeof(header));
        if (size < sn_size) {
                if (size >= 0) {
                        return -ENODATA;
                }

                return size;
        }

        if (ctx->type == RLC_AM) {
                pdu->flags.is_status = (~(header[0] >> 7)) & 1;
                if (pdu->flags.is_status) {
                        status = decode_status_header_(ctx, pdu, header);
                        goto done;
                }

                pdu->flags.polled = (header[0] >> 6) & 1;

                from_si_(pdu, (header[0] >> 4) & 0x3);

                if (has_sn_(pdu, ctx->type)) {
                        if (ctx->conf->sn_width == RLC_SN_12BIT) {
                                pdu->sn =
                                        ((header[0] & 0xf) << 8) | (header[1]);
                        } else {
                                pdu->sn = ((header[0] & 0x3) << 16) |
                                          (header[1] << 8) | header[2];
                        }
                }
        } else {
                from_si_(pdu, (header[0] >> 6) & 0x3);

                if (has_sn_(pdu, ctx->type)) {
                        if (ctx->conf->sn_width == RLC_SN_6BIT) {
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

done:
        if (status == 0) {
                gabs_pbuf_strip_head(buf, rlc_pdu_header_size(ctx, pdu));
        }

        return status;
}

size_t rlc_pdu_header_size(const struct rlc_context *ctx,
                           const struct rlc_pdu *pdu)
{
        switch (ctx->type) {
        case RLC_AM:
        case RLC_UM:
                return sn_num_bytes_(ctx->conf->sn_width) +
                       (SO_SIZE_ * has_so_(pdu));
        case RLC_TM:
                return 0;
        default:
                assert(0);
                return 0;
        }
}

void rlc_status_encode(struct rlc_context *ctx,
                       const struct rlc_pdu_status *status, gabs_pbuf *buf)
{
        size_t full_width;
        size_t sn_width;
        uint8_t ext;
        uint8_t data[RLC_STATUS_MAX_SIZE];

        (void)memset(data, 0, sizeof(data));

        full_width = 0;
        sn_width = sn_num_bits_(ctx->conf->sn_width);

        bit_copy_mem_(data, status->nack_sn, full_width, sn_width);
        full_width += sn_width;

        ext = (status->ext.has_more << 2) | (status->ext.has_offset << 1) |
              (status->ext.has_range << 0);
        bit_copy_mem_(data, ext, full_width, 3);
        full_width += 3;

        full_width = width_aligned_(full_width);
        if (status->ext.has_offset) {
                bit_copy_mem_(data, status->offset.start, full_width,
                              SO_WIDTH_);
                full_width += SO_WIDTH_;
                bit_copy_mem_(data, status->offset.end, full_width, SO_WIDTH_);
                full_width += SO_WIDTH_;
        }

        if (status->ext.has_range) {
                bit_copy_mem_(data, status->range, full_width, 8);
                full_width += 8;
        }

        gabs_pbuf_put(buf, data, bytes_ceil_(full_width));
}

rlc_errno rlc_status_decode(struct rlc_context *ctx,
                            struct rlc_pdu_status *status, gabs_pbuf *buf)
{
        uint8_t header[RLC_STATUS_MAX_SIZE];
        size_t req_size;
        ptrdiff_t size;
        uint8_t ext;

        req_size = sn_num_bytes_(ctx->conf->sn_width);

        size = gabs_pbuf_copy(*buf, header, 0, sizeof(header));
        if (size < req_size) {
                return -ENODATA;
        }

        if (ctx->conf->sn_width == RLC_SN_12BIT) {
                status->nack_sn = (header[0] << 4) | ((header[1] >> 4) & 0xf);

                ext = (header[1] >> 1) & 0x7;
        } else {
                status->nack_sn = (header[0] << 10) | (header[1] << 2) |
                                  ((header[2] >> 6) & 0x3);

                ext = (header[2] >> 3) & 0x7;
        }

        status->ext.has_more = (ext >> 2) & 0x1;
        status->ext.has_offset = (ext >> 1) & 0x1;
        status->ext.has_range = (ext >> 0) & 0x1;

        if (status->ext.has_offset) {
                req_size += (SO_WIDTH_ / 8) * 2;

                if (size < req_size) {
                        return -ENODATA;
                }

                status->offset.start =
                        (header[req_size - 4] << 8) | (header[req_size - 3]);
                status->offset.end =
                        (header[req_size - 2] << 8) | (header[req_size - 1]);
        }

        if (status->ext.has_range) {
                req_size += 1;
                if (size < req_size) {
                        return -ENODATA;
                }

                status->range = header[req_size - 1];
        }

        gabs_pbuf_strip_head(buf, req_size);

        return 0;
}

size_t rlc_status_size(const struct rlc_context *ctx,
                       struct rlc_pdu_status *status)
{
        size_t ret;

        ret = sn_num_bytes_(ctx->conf->sn_width);
        if (status->ext.has_offset) {
                ret += (SO_WIDTH_ / 8) * 2;
        }

        if (status->ext.has_range) {
                ret += 1;
        }

        return ret;
}
