
#ifndef RLC_LOG_H__
#define RLC_LOG_H__

#include <rlc/decl.h>
#include <rlc/plat.h>

RLC_BEGIN_DECL

#define rlc_dbgf(...) rlc_plat_dbgf(__VA_ARGS__)
#define rlc_inff(...) rlc_plat_inff(__VA_ARGS__)
#define rlc_wrnf(...) rlc_plat_wrnf(__VA_ARGS__)
#define rlc_errf(...) rlc_plat_errf(__VA_ARGS__)

RLC_END_DECL

#include <rlc_log.h>

#endif /* RLC_LOG_H__ */
