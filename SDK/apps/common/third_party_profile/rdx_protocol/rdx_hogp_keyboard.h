#ifndef _RDX_HOGP_KEYBOARD_H_
#define _RDX_HOGP_KEYBOARD_H_

#include "system/includes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RDX_HOGP_KEYBOARD_REPORT_LEN  8

typedef struct {
    u8 modifiers;
    u8 reserved;
    u8 usages[6];
} rdx_hogp_keyboard_report_t;

void rdx_hogp_keyboard_runtime_reset(void);
void rdx_hogp_keyboard_ready_drop_cleanup(void);
u16 rdx_hogp_keyboard_att_read(u16 att_handle, u16 offset,
                               u8 *buffer, u16 buffer_size);
int rdx_hogp_keyboard_att_write(u16 offset, const u8 *buffer,
                                u16 buffer_size);
int rdx_hogp_keyboard_report_send(
    const rdx_hogp_keyboard_report_t *report);
int rdx_hogp_keyboard_release_all(void);
u8 rdx_hogp_keyboard_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif
