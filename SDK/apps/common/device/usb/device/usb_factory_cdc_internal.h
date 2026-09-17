#ifndef T2620_USB_FACTORY_CDC_INTERNAL_H
#define T2620_USB_FACTORY_CDC_INTERNAL_H
#include "usb/device/usb_factory_cdc.h"
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
struct usb_device_t;
#define FACTORY_CDC_TX_DEPTH 4
#define FACTORY_CDC_PACKET 64
#define FACTORY_CDC_TIMEOUT_MS 2000
enum factory_cdc_reason {
    FACTORY_CDC_START, FACTORY_CDC_RESET, FACTORY_CDC_CONFIG,
    FACTORY_CDC_CLOSE, FACTORY_CDC_STOP, FACTORY_CDC_TIMEOUT
};
/* Internal lifecycle hooks, callable from USB ISR. Nested IRQ exclusion is
 * supported by BR28; each hook protects its own shared transport state. */
void factory_cdc_invalidate(enum factory_cdc_reason reason);
void factory_cdc_open(void);
void factory_cdc_rx_irq(void);
/* usb_stack lifecycle and work dispatch only. */
int factory_cdc_start(void);
void factory_cdc_quiesce(void);
void factory_cdc_stop(void);
void factory_cdc_process(void);
int factory_cdc_needs_restart(void);

/* Driver primitives: usb_stack only, under local IRQ exclusion. Never wait.
 * read consumes at most one packet into a 64-byte buffer; write returns
 * -1 when busy, otherwise exact accepted length (including a ZLP's 0). */
int cdc_factory_read_packet(u8 *bytes);
int cdc_factory_write_packet(const u8 *bytes, u32 length);
int cdc_factory_tx_busy(void);
int cdc_factory_configured(void);
void cdc_factory_configuration(struct usb_device_t *device, u32 value);
#if TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
int factory_cdc_test_receive(u32 generation, const u8 *bytes, u32 length);
void factory_cdc_test_open(u32 generation);
#endif
#endif
#endif
