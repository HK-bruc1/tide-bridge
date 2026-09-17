#include "usb/device/usb_factory_cdc_internal.h"
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE && TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
/* Optional byte-test consumer. No DUT commands or transport-owned state. */
int factory_cdc_test_receive(u32 generation, const u8 *bytes, u32 length)
{
    return factory_cdc_send(generation, bytes, length);
}
void factory_cdc_test_open(u32 generation)
{
    /* usb_stack only; send copies the buffer before returning. */
    static u8 pattern[256];
    for (u32 i = 0; i < sizeof(pattern); ++i) pattern[i] = (u8)i;
    factory_cdc_send(generation, pattern, sizeof(pattern));
}
#endif
