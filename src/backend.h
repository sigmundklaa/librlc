
#ifndef RLC_BACKEND_H__
#define RLC_BACKEND_H__

#include <rlc/rlc.h>

#include "methods.h"

RLC_BEGIN_DECL

ssize_t rlc_backend_tx_submit(struct rlc_context *ctx, struct rlc_pdu *pdu,
                              rlc_buf *buf);

RLC_END_DECL

#endif /* RLC_BACKEND_H__ */
