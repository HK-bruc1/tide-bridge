#include "rdx_default_hooks.h"

void rdx_hook_led_on(u8 led_id)
{
	(void)led_id;
}

void rdx_hook_led_off(u8 led_id)
{
	(void)led_id;
}

void rdx_hook_led_blink(u8 led_id, u32 period_ms)
{
	(void)led_id;
	(void)period_ms;
}

void rdx_hook_motor_start(u32 duration_ms)
{
	(void)duration_ms;
}

void rdx_hook_motor_stop(void)
{
}

void rdx_hook_dut_enter(void)
{
}

void rdx_hook_dut_exit(void)
{
}

bool rdx_hook_dut_is_active(void)
{
	return false;
}

void rdx_hook_diag_dump_state(void)
{
}
