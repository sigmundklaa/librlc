
#ifndef RLC_BACKEND_H__
#define RLC_BACKEND_H__

#include <rlc/rlc.h>

RLC_BEGIN_DECL

/**
 * @brief Submit @p buf, with a header for @p pdu, to the lower layer (backend).
 *
 * This steals a reference to @p buf, and the backend assumes to be the owner
 * of the buffer after this call. As such, it may be modified and is not
 * safe to use after this call.
 *
 * @return ptrdiff_t
 * @retval >= 0 Written bytes
 * @retval <0 Negative `rlc_errno` code.
 */
ptrdiff_t rlc_backend_tx_submit(struct rlc_context *ctx, struct rlc_pdu *pdu,
                                rlc_buf *buf);

/**
 * @brief Request a transmission opportunity from the lower layer.
 *
 * If @p offload is `true`, the request will be offloaded to another thread
 * such that it is safe to call in a recursive context.
 *
 * @param ctx
 * @param offload Whether or not to offload to another thread
 */
void rlc_backend_tx_request(struct rlc_context *ctx, bool offload);

RLC_END_DECL

#endif /* RLC_BACKEND_H__ */
