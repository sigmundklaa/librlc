
#ifndef RLC_BACKEND_H__
#define RLC_BACKEND_H__

#include <gabs/pbuf.h>

#include <rlc/utils.h>
#include <rlc/pdu.h>

RLC_BEGIN_DECL

struct rlc_context;

struct rlc_backend {
        rlc_errno (*tx_submit)(struct rlc_context *, gabs_pbuf);
        rlc_errno (*tx_request)(struct rlc_context *);
};

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
                                gabs_pbuf buf);

/**
 * @brief Request a transmission opportunity from the lower layer.
 */
void rlc_backend_tx_request(struct rlc_context *ctx);

RLC_END_DECL

#endif /* RLC_BACKEND_H__ */
