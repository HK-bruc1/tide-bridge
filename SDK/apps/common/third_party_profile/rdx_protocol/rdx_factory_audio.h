#ifndef RDX_FACTORY_AUDIO_H
#define RDX_FACTORY_AUDIO_H
#include "app_config.h"
#include "typedef.h"
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE && !TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE
int rdx_factory_audio_command(u32 generation, const char *line, char *out, unsigned capacity);
void rdx_factory_audio_service(u32 generation);
void rdx_factory_audio_send(u32 generation);
int rdx_factory_audio_begin(u32 generation, u32 id);
int rdx_factory_audio_ready(u32 generation);
int rdx_factory_audio_owned(void);
int rdx_factory_audio_needs_service(void);
int rdx_factory_audio_tx_pending(u32 generation);
int rdx_factory_audio_started(void);
void rdx_factory_audio_frame(const u8 *data, u32 length);
void rdx_factory_audio_saved(int result);
void rdx_factory_audio_stopped(u32 id, int result);
int rdx_factory_audio_file_busy(void);
/* Returns -1 with storage quarantined; caller must reset, not power SD off. */
int rdx_factory_audio_shutdown(void);
u32 rdx_factory_audio_crc(u32 crc, const u8 *data, u32 length);
#else
static inline int rdx_factory_audio_command(u32 g, const char *l, char *o, unsigned c) { return 0; }
static inline void rdx_factory_audio_service(u32 g) {}
static inline void rdx_factory_audio_send(u32 g) {}
static inline int rdx_factory_audio_begin(u32 g, u32 id) { return -1; }
static inline int rdx_factory_audio_ready(u32 g) { return 0; }
static inline int rdx_factory_audio_owned(void) { return 0; }
static inline int rdx_factory_audio_needs_service(void) { return 0; }
static inline int rdx_factory_audio_started(void) { return 0; }
static inline void rdx_factory_audio_frame(const u8 *d, u32 n) {}
static inline void rdx_factory_audio_saved(int r) {}
static inline void rdx_factory_audio_stopped(u32 id, int r) {}
static inline int rdx_factory_audio_file_busy(void) { return 0; }
static inline int rdx_factory_audio_shutdown(void) { return 0; }
#endif
#endif
