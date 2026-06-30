#ifndef __RDX_BOARD_CONFIG_H__
#define __RDX_BOARD_CONFIG_H__

#include "rdx_board_config_types.h"

#ifndef RDX_CHIP_FAMILY
#define RDX_CHIP_FAMILY     "jl7018"
#endif

#ifndef RDX_BOARD_NAME
#define RDX_BOARD_NAME      "t2616_ep"
#endif

const rdx_board_config_t *rdx_board_get_config(void);

#endif
