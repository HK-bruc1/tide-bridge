#ifndef __RDX_DEFAULT_HOOKS_H__
#define __RDX_DEFAULT_HOOKS_H__

#include "typedef.h"

int  rdx_hook_led_init(void)                __attribute__((weak));
void rdx_hook_led_on(u8 led_id)           __attribute__((weak));
void rdx_hook_led_off(u8 led_id)          __attribute__((weak));
void rdx_hook_led_blink(u8 led_id, u32 period_ms) __attribute__((weak));
void rdx_hook_led_set_scene(u8 scene)           __attribute__((weak));
void rdx_hook_led_set_charge_state_by_battery(u8 battery_percent) __attribute__((weak));
void rdx_hook_led_restore_system_state(void)    __attribute__((weak));

void rdx_hook_motor_start(u32 duration_ms) __attribute__((weak));
void rdx_hook_motor_stop(void)             __attribute__((weak));
bool rdx_hook_motor_is_running(void)       __attribute__((weak));

void rdx_hook_dut_enter(void)              __attribute__((weak));
void rdx_hook_dut_exit(void)               __attribute__((weak));
bool rdx_hook_dut_is_active(void)          __attribute__((weak));

void rdx_hook_diag_dump_state(void)        __attribute__((weak));

#endif
