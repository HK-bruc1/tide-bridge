#include "app_config.h"
#include "rdx_dip_switch.h"

#if TCFG_DIP_SWITCH_POWER_ENABLE

#include "system/includes.h"
#include "gpio.h"
#include "power/power_wakeup.h"
#include "poweroff.h"

#ifndef TCFG_DIP_SWITCH_POWER_IO
#define TCFG_DIP_SWITCH_POWER_IO    IO_PORTB_01
#endif

static bool s_init_done = false;

static void rdx_dip_switch_deferred_handle(void *priv)
{
    (void)priv;

    // Debounce
    os_time_dly(2);

    int level = gpio_read(TCFG_DIP_SWITCH_POWER_IO);

    if (level == 1) {
        // OFF: switch toggled to HIGH -- initiate shutdown
        r_printf("[DIP] OFF -> powering down\n");

        // Switch edge to FALLING_EDGE so OFF (HIGH) state won't self-wakeup,
        // but keep wakeup ENABLED so ON (HIGH->LOW) can still wake the device.
        p33_io_wakeup_edge(TCFG_DIP_SWITCH_POWER_IO, FALLING_EDGE);

        // Ensure stable pull-up for clean wakeup edge
        gpio_set_mode(IO_PORT_SPILT(TCFG_DIP_SWITCH_POWER_IO), PORT_INPUT_PULLUP_10K);

        // Final confirmation after short delay
        os_time_dly(1);

        if (gpio_read(TCFG_DIP_SWITCH_POWER_IO) == 1) {
            sys_enter_soft_poweroff(POWEROFF_NORMAL);
        } else {
            // User flipped back to ON quickly
            p33_io_wakeup_edge(TCFG_DIP_SWITCH_POWER_IO, RISING_EDGE);
            r_printf("[DIP] aborted, back ON\n");
        }
    } else {
        // ON: switch toggled to LOW -- flip edge to detect next OFF
        p33_io_wakeup_edge(TCFG_DIP_SWITCH_POWER_IO, RISING_EDGE);
    }
}

// P33 ISR callback: post deferred handler to app_core
void rdx_dip_switch_p33_irq(P33_IO_WKUP_EDGE edge)
{
    int msg[2];

    if (!s_init_done) {
        return;
    }

    msg[0] = (int)rdx_dip_switch_deferred_handle;
    msg[1] = 0;
    os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
}

void rdx_dip_switch_init(void)
{
    if (s_init_done) {
        return;
    }
    s_init_done = true;

    os_time_dly(2);

    int level = gpio_read(TCFG_DIP_SWITCH_POWER_IO);

    // Set edge to opposite of current level so next flip is detected
    if (level == 1) {
        // Currently OFF -- keep FALLING_EDGE for wakeup, then shutdown
        r_printf("[DIP] init OFF -> shutting down\n");
        p33_io_wakeup_edge(TCFG_DIP_SWITCH_POWER_IO, FALLING_EDGE);
        gpio_set_mode(IO_PORT_SPILT(TCFG_DIP_SWITCH_POWER_IO), PORT_INPUT_PULLUP_10K);
        sys_enter_soft_poweroff(POWEROFF_NORMAL);
    } else {
        // Currently ON -- set RISING_EDGE to detect next OFF
        r_printf("[DIP] init ON\n");
        p33_io_wakeup_edge(TCFG_DIP_SWITCH_POWER_IO, RISING_EDGE);
    }
}

#endif // TCFG_DIP_SWITCH_POWER_ENABLE
