#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_peripheral_power.data.bss")
#pragma data_seg(".rdx_peripheral_power.data")
#pragma const_seg(".rdx_peripheral_power.text.const")
#pragma code_seg(".rdx_peripheral_power.text")
#endif

#include "app_config.h"
#include "asm/dac.h"
#include "gpio_config.h"

#include "rdx_app_config.h"
#include "rdx_peripheral_power.h"

#if TCFG_T2620_AMP_POWER_ENABLE && RDX_WIFI_ENABLE
#error "T2620 PE5 amplifier enable conflicts with the legacy RDX WiFi SPI CS assignment"
#endif

#if TCFG_T2620_AMP_POWER_ENABLE

static u8 g_rdx_amp_enabled;

void rdx_peripheral_power_amp_set(u8 enable)
{
    u8 next = !!enable;

    if (g_rdx_amp_enabled == next) {
        return;
    }

    gpio_set_mode(IO_PORT_SPILT(TCFG_T2620_AMP_ENABLE_IO),
                  next ? PORT_OUTPUT_HIGH : PORT_OUTPUT_LOW);
    g_rdx_amp_enabled = next;
}

u8 rdx_peripheral_power_amp_is_enabled(void)
{
    return g_rdx_amp_enabled;
}

/*
 * JL's DAC implementation calls this application override synchronously.
 * Keep the external amplifier disabled until the DAC analog path is ready,
 * and disable it before the DAC analog path starts closing.
 */
void audio_dac_power_state(u8 state)
{
    switch (state) {
    case DAC_ANALOG_OPEN_FINISH:
        rdx_peripheral_power_amp_set(1);
        break;

    case DAC_ANALOG_OPEN_PREPARE:
    case DAC_ANALOG_CLOSE_PREPARE:
    case DAC_ANALOG_CLOSE_FINISH:
    default:
        rdx_peripheral_power_amp_set(0);
        break;
    }
}

#else

void rdx_peripheral_power_amp_set(u8 enable)
{
    (void)enable;
}

u8 rdx_peripheral_power_amp_is_enabled(void)
{
    return 0;
}

#endif /* TCFG_T2620_AMP_POWER_ENABLE */
