#ifndef __RDX_PRODUCT_SELECT_H__
#define __RDX_PRODUCT_SELECT_H__

#if (RDX_AI_SEL_APP == APP_NEVIEW_EN)
#include "product/neview.h"
#elif (RDX_AI_SEL_APP == APP_NINGQU_EN)
#include "product/ningqu.h"
#elif (RDX_AI_SEL_APP == APP_NOTTA_EN)
#include "product/notta.h"
#elif (RDX_AI_SEL_APP == APP_TINGNAO_EN)
#include "product/tingnao.h"
#elif (RDX_AI_SEL_APP == APP_JMEASY_EN)
#include "product/jmeasy.h"
#elif (RDX_AI_SEL_APP == APP_SHENGLANG_EN)
#include "product/shenglang.h"
#elif (RDX_AI_SEL_APP == APP_AITIR_EN)
#include "product/aitir.h"
#elif (RDX_AI_SEL_APP == APP_YYS_EN)
#include "product/yys.h"
#elif (RDX_AI_SEL_APP == APP_LYNSE_EN)
#include "product/lynse.h"
#elif (RDX_AI_SEL_APP == APP_TURING_EN)
#include "product/turing.h"
#elif (RDX_AI_SEL_APP == APP_RAYCON_EN)
#include "product/raycon.h"
#elif (RDX_AI_SEL_APP == APP_CDJY_EN)
#include "product/cdjy.h"
#elif (RDX_AI_SEL_APP == APP_BRANDWORKS_EN)
#include "product/brandworks.h"
#elif (RDX_AI_SEL_APP == APP_FINDAI_EN)
#include "product/findai.h"
#elif (RDX_AI_SEL_APP == APP_BEANSTALK_EN)
#include "product/beanstalk.h"
#elif (RDX_AI_SEL_APP == APP_ZENCHORD_EN)
#include "product/zenchord.h"
#elif (RDX_AI_SEL_APP == APP_DEEPMINER_EN)
#include "product/deepminer.h"
#else
#error "RDX_AI_SEL_APP has no product config"
#endif

#endif
