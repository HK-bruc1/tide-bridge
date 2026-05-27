#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "audio_type.h"

/*
 *  板级配置选择
 */

#define CONFIG_BOARD_AC701N_DEMO
//#define CONFIG_BOARD_AC7016F_EARPHONE
// #define CONFIG_BOARD_AC701N_ANC

#include "media/audio_def.h"
#include "board_ac701n_demo_cfg.h"

#define  DUT_AUDIO_DAC_LDO_VOLT                 DACVDD_LDO_1_35V

#endif
