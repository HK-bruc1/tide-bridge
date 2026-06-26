#ifndef __RDX_CONFIG_VALIDATE_H__
#define __RDX_CONFIG_VALIDATE_H__

/*
 * Stage 1 compile-time validation for RDX product/board/chip/app combinations.
 * Keep this file ASCII-only to avoid encoding issues in the legacy CRLF/GDK build.
 */

/* 1. RDX_AI_SEL_APP must be one of the defined APP_*_EN bits */
#define _RDX_APP_MASK (APP_NEVIEW_EN | APP_XLSW_EN | APP_GNT_EN | APP_XYZL_EN | \
                       APP_NINGQU_EN | APP_NOTTA_EN | APP_TINGNAO_EN | APP_JMEASY_EN | \
                       APP_SHENGLANG_EN | APP_AITIR_EN | APP_YYS_EN | APP_LYNSE_EN | \
                       APP_TURING_EN | APP_RAYCON_EN | APP_CDJY_EN | APP_BRANDWORKS_EN | \
                       APP_FINDAI_EN | APP_WAN_EN | APP_BEANSTALK_EN | APP_ZENCHORD_EN | \
                       APP_DEEPMINER_EN | APP_TTEASY_EN | APP_AISPEECH_EN | APP_SHUGUO_EN | \
                       APP_VASCO_EN | APP_ABC_EN | APP_MLAMPWXB_EN)

_Static_assert((RDX_AI_SEL_APP & _RDX_APP_MASK) != 0,
               "RDX_AI_SEL_APP must be one of APP_*_EN");

/* Single-APP check: skipped when multi-APP feature mode is enabled */
#ifndef RDX_MULTI_FUNC_INTERFACE
_Static_assert((RDX_AI_SEL_APP & (RDX_AI_SEL_APP - 1)) == 0,
               "RDX_AI_SEL_APP must select exactly one APP");
#endif

/* 2. RDX_SEL_DEVICE must be one of the defined DEVICE_* values (equality check, not bitmask) */
#if (RDX_SEL_DEVICE != DEVICE_RDX_EP_A9) && \
    (RDX_SEL_DEVICE != DEVICE_RDX_BJ_T2403) && \
    (RDX_SEL_DEVICE != DEVICE_DACOM_EP_T2401) && \
    (RDX_SEL_DEVICE != DEVICE_DACOM_CC_T2401) && \
    (RDX_SEL_DEVICE != DEVICE_1MORE_EP_T2402) && \
    (RDX_SEL_DEVICE != DEVICE_1MORE_CC_T2402) && \
    (RDX_SEL_DEVICE != DEVICE_ZENCORD_EP_T2616) && \
    (RDX_SEL_DEVICE != DEVICE_ZENCORD_CC_T2616)
#error "RDX_SEL_DEVICE must be one of DEVICE_*"
#endif

/* 3. Known invalid APP/DEVICE combination */
#if (RDX_AI_SEL_APP == APP_ZENCHORD_EN) && \
    ((RDX_SEL_DEVICE != DEVICE_ZENCORD_EP_T2616) && \
     (RDX_SEL_DEVICE != DEVICE_ZENCORD_CC_T2616))
#error "APP_ZENCHORD_EN must pair with DEVICE_ZENCORD_*_T2616"
#endif

#endif /* __RDX_CONFIG_VALIDATE_H__ */
