#ifndef RDX_CDC_FILE_H
#define RDX_CDC_FILE_H
#include "app_config.h"
#include "typedef.h"
#if TCFG_T2620_CDC_FILE_ENABLE
int rdx_cdc_file_active(void);
void rdx_cdc_file_session(u32 generation);
int rdx_cdc_file_command(u32 generation, const char *line, int invalid, char *out, unsigned capacity);
void rdx_cdc_file_service(u32 generation);
int rdx_cdc_file_reply(char *out, unsigned capacity);
u32 rdx_cdc_file_crc(u32 crc, const u8 *bytes, u32 length);
#endif
#endif
