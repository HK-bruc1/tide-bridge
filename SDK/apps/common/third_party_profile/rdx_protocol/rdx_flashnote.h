#ifndef _RDX_FLASHNOTE_H_
#define _RDX_FLASHNOTE_H_

#include "rdx_ble_session.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* app_core request entry. The protocol callback only validates and captures
 * the owner token; all recording business runs on app_core. */
int rdx_flashnote_request(u8 command, const rdx_ble_async_token_t *token);

/* Exact parser used by the patched protocol object. command is ASCII '0'/'1'. */
u8 rdx_flashnote_protocol_parse(u8 *data, u16 len, u8 *command);

/* Called synchronously by rdx_record_run_init() after the online recorder is
 * initialized. APP flashnote is stream-only, so it has no file identity and
 * never publishes record_start. Returns nonzero if RUNNING could not be sent. */
int rdx_flashnote_on_stream_started(
    u32 session_generation, u8 format,
    const rdx_ble_async_token_t *token);
void rdx_flashnote_on_record_init_failed(int err);

/* F19 stream-only flashnote state. No file identity and no record_start event
 * are published because this route never creates a local file. START is sent
 * only after the online recorder is initialized; END is sent after its worker
 * has completed cleanup. */
int rdx_flashnote_on_key_record_started(
    u32 session_generation, u8 format,
    const rdx_ble_async_token_t *token);
/* F19 offline registration. It records the real file identity but deliberately
 * sends nothing and does not bind the local recording to a BLE owner. */
int rdx_flashnote_on_key_local_record_started(
    u32 session_generation, u8 format, u32 sn, const char *filename);
/* Reconnect snapshot for an active offline F19 session. This only updates APP
 * presentation and never changes the fixed local route. */
int rdx_flashnote_sync_active(const rdx_ble_async_token_t *token);
void rdx_flashnote_on_key_record_stopped(
    u32 session_generation, u8 format,
    const rdx_ble_async_token_t *token);
void rdx_flashnote_on_key_local_record_stopped(
    u32 session_generation, u8 format);

/* Transport loss paths invalidate the owner before any asynchronous release.
 * They never emit END because the receiver is no longer authoritative. */
void rdx_flashnote_cancel_all(void);
void rdx_flashnote_cancel_for_token(const rdx_ble_async_token_t *token);

#ifdef __cplusplus
}
#endif

#endif
