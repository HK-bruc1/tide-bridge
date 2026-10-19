#ifndef T2620_USB_FACTORY_CDC_H
#define T2620_USB_FACTORY_CDC_H

#include "typedef.h"
#include "app_config.h"

#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
#define FACTORY_CDC_TX_MAX 258

enum factory_cdc_result {
    FACTORY_CDC_OK = 0, FACTORY_CDC_STALE = -1,
    FACTORY_CDC_FULL = -2, FACTORY_CDC_INVALID = -3
};
/* usb_stack task only, nonblocking. Bytes live only during this callback.
 * Return 0 to accept; otherwise the SAME bytes are offered on a later tick.
 * No DUT/protocol work here: a future app_core consumer must copy and queue.
 * No synchronous calls to usb_stack from the callback. */
typedef int (*factory_cdc_rx_cb)(u32 generation, const u8 *bytes, u32 length);
/* Register before start; stop/restart preserves this consumer. */
void factory_cdc_set_rx_handler(factory_cdc_rx_cb handler);
/* 仅发送中断安全的通知，可能在调用方的关中断临界区内执行；
 * 将任务投递给 app_core，不在此处执行协议、定时器或文件操作。 */
void factory_cdc_set_state_handler(void (*handler)(void));
/* Copies all bytes or rejects all. Success means queued, NOT host received.
 * May be called by tasks; generation must come from this session's callback
 * or snapshot. Capacity is bounded; never retries an expired generation. */
int factory_cdc_send(u32 generation, const u8 *bytes, u32 length);
#if TCFG_T2620_CDC_FILE_ENABLE
int factory_cdc_stream_send(u32 generation, const u8 *bytes, u32 length);
void factory_cdc_set_tx_handler(void (*handler)(void));
#endif
u32 factory_cdc_generation(void); /* 0 while not ready */
/* Invalidate only this session, without waiting or releasing USB resources.
 * usb_stack observes restart on its next tick and owns reenumeration. */
void factory_cdc_abort(u32 generation);

#endif
#endif
