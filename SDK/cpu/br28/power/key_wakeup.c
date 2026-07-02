#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".key_wakeup.data.bss")
#pragma data_seg(".key_wakeup.data")
#pragma const_seg(".key_wakeup.text.const")
#pragma code_seg(".key_wakeup.text")
#endif
#include "asm/power_interface.h"
#include "app_config.h"
#include "gpio.h"
#include "iokey.h"
#include "adkey.h"

#if TCFG_DIP_SWITCH_POWER_ENABLE
#include "rdx_dip_switch.h"
#endif

void key_active_set(u8 port);

#if !TCFG_DIP_SWITCH_POWER_ENABLE
static void key_wakeup_callback(P33_IO_WKUP_EDGE edge)
{
    key_active_set(0);
}
#endif

static struct _p33_io_wakeup_config port0 = {
    .pullup_down_mode = PORT_INPUT_PULLUP_10K,
    .filter      		= PORT_FLT_DISABLE,
    .edge               = FALLING_EDGE,
    .gpio               = IO_PORTB_01,
#if TCFG_DIP_SWITCH_POWER_ENABLE
    .callback			= rdx_dip_switch_p33_irq,
#else
    .callback			= key_wakeup_callback,
#endif
};

void key_wakeup_init()
{
#if !TCFG_DIP_SWITCH_POWER_ENABLE
#if TCFG_ADKEY_ENABLE
    const struct adkey_platform_data *data = get_adkey_platform_data();
    port0.gpio = data->adkey_pin;
#endif

#if TCFG_IOKEY_ENABLE
    const struct iokey_platform_data *data = get_iokey_platform_data();
    u8 wakeup_key_num = 0;      //默认0号按键做唤醒，需要可以自行修改按键号
    if (data->port[wakeup_key_num].connect_way == ONE_PORT_TO_LOW) {
        port0.edge  = FALLING_EDGE;
        port0.gpio  = data->port[wakeup_key_num].key_type.one_io.port;
    } else if (data->port[wakeup_key_num].connect_way == ONE_PORT_TO_HIGH) {
        port0.edge  = RISING_EDGE;
        port0.gpio  = data->port[wakeup_key_num].key_type.one_io.port;
    } else if (data->port[wakeup_key_num].connect_way == DOUBLE_PORT_TO_IO) {
        port0.edge  = FALLING_EDGE;
        port0.gpio  =  data->port[wakeup_key_num].key_type.two_io.in_port;
    }
#endif
#endif // !TCFG_DIP_SWITCH_POWER_ENABLE
    p33_io_wakeup_port_init(&port0);
    p33_io_wakeup_enable(port0.gpio, 1);
}
