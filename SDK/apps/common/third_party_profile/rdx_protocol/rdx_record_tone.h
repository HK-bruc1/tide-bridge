#ifndef RDX_RECORD_TONE_H
#define RDX_RECORD_TONE_H

#include "typedef.h"

#define RDX_RECORD_TONE_CUSTOM_CMD "rectone"

void rdx_record_tone_init(void);
u8 rdx_record_tone_enabled(void);
void rdx_record_tone_handle_custom(const char *value);
/* app_core only; successful full reset locks SET until reboot. */
int rdx_record_tone_factory_reset(void);
/* 录音类配置（提示音/落盘）共用的 SET 忙判断，app_core only。 */
u8 rdx_record_config_busy(void);

#endif
