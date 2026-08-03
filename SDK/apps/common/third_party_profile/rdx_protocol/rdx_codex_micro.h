#ifndef _RDX_CODEX_MICRO_H_
#define _RDX_CODEX_MICRO_H_

#include "system/includes.h"

void rdx_codex_micro_init(void);
void rdx_codex_micro_deinit(void);
void rdx_codex_micro_ready_drop_cleanup(void);
void rdx_codex_micro_process_json(const char *json, u16 len);
int rdx_codex_micro_fast_key_click(void);
void rdx_codex_micro_fast_key_release_all(void);

#endif
