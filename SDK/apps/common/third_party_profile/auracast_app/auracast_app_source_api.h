#ifndef _AURACAST_APP_SOURCE_API_H_
#define _AURACAST_APP_SOURCE_API_H_

#include "system/includes.h"

#define AURACAST_NAME_MAX_LEN               32
#define AURACAST_PASSWORD_MAX_LEN           32
#define AURACAST_CODE_MAX_LEN               16

struct _auracast_tx_vm_config {
    u8 auracast_password[32];
    u8 auracast_name[32];
    u8 auracast_broadcast_code[16];
    u8 auracast_encryption_en;
    u8 auracast_password_len;
    u8 auracast_name_len;
    u8 auracast_audio_index;
    u8 auracast_tx_power_level;
    u8 reserve_1;
    u8 reserve_2;
    u8 reserve_3;
};

extern struct _auracast_tx_vm_config *tx_config;

extern int auracast_app_read_vm_config(struct _auracast_tx_vm_config *config);
extern void auracast_app_reset_vm_config(struct _auracast_tx_vm_config *config);
extern int auracast_app_login_authentication_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len);
extern int auracast_app_login_password_set_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len);
extern int auracast_app_broadcast_setting_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len);
extern void auracast_app_cpu_reset_do(void *priv);
extern int auracast_app_broadcast_reset_deal(u8 opcode, u8 sn, u8 *payload, u32 payload_len);

extern int auracast_app_source_api_init(void);
extern int auracast_app_source_api_uninit(void);

#endif
