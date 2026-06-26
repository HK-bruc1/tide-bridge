/*=====================================================================================
 HEADER NAME: rdx_vm.h
 MODULE NAME: rdx vm module header.
 
 GENERAL DESCRIPTION: 	
 	This File declares the interface for VM (non-volatile memory) operations 
    in the RDX application.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2026-02-26 
 LastEditors: sheng.dong
 LastEditTime: 2026-02-26
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_vm.h
  
 Self-documenting Code
=====================================================================================*/

#ifndef _RDX_VM_H_
#define _RDX_VM_H_

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* Include files
******************************************************************************/ 
#include "typedef.h"
#include "rdx_commonDef.h"
#include "rdx_app_config.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define RDX_FACTORY_AUTHENTICATOR_TOTAL_SIZE        (54) //24 + 12 + 16 + 2
#define RDX_BLE_DEVICE_AUTH_KEY_SIZE                (24)
#define RDX_BLE_MAC_STRING_SIZE                     (12) //Mac无冒号
#define RDX_LABEL_SN_SIZE                           (16)

/*******************************************************************************
* Structure and Enum Section
*******************************************************************************/
typedef struct {
    u8 bound_state;
} rdx_bound_info_t;

typedef struct {
    u8 AuthKey[RDX_BLE_DEVICE_AUTH_KEY_SIZE + 1];
    u8 ble_mac_hex[6];
    char ble_mac_str[RDX_BLE_MAC_STRING_SIZE + 1];
    u8 label_sn[RDX_LABEL_SN_SIZE + 1];
} rdx_auth_info_t;

typedef struct{
    u16 dev_type;
    u8 auth[RDX_BLE_DEVICE_AUTH_KEY_SIZE + 1];
    u8 ble_mac[6];
    char ble_mac_str[18]; //带冒号
    u8 bt_mac[6];
    char bt_mac_str[18];
    u8 wifi_mac[6];
    char wifi_mac_str[18];
    u8 label_sn[RDX_LABEL_SN_SIZE + 1];
}DevBaseInfo;

#if RDX_PRODUCT_IS_CHARGE_CASE
typedef struct{
    u8 ep_mac[6];
    char ep_mac_str[RDX_BLE_MAC_STRING_SIZE + 1];
}EarphoneInfo;
#endif

/*******************************************************************************
* Function Declaration Section
*******************************************************************************/
void rdx_vm_init(void);
u8 rdx_vm_get_bound_status(void);
void rdx_vm_set_bound_status(u8 d, u8 show_en);
void rdx_vm_bound_status_check(void);
u8  rdx_vm_is_unbouding(void);
void rdx_vm_set_unbounding(u8 v);
void rdx_vm_unbound_cb(u8 result);
void rdx_vm_unbound_handle(void);
void rdx_vm_choose_to_unbound_cb(u8 result);
void rdx_vm_choose_to_unbound_handle(int usr_para, int format_en);
void rdx_vm_sys_reset_to_defaults(void);

const u8 *rdx_vm_get_license_ptr(void);
u8 rdx_vm_read_product_info_from_flash(u8 *read_buf, u16 buflen);
void rdx_vm_auth_info_init(void);
rdx_auth_info_t* rdx_vm_get_auth_info(void);

#if RDX_PRODUCT_IS_CHARGE_CASE
EarphoneInfo* rdx_vm_get_ep_info(void);
int  rdx_vm_write_ep_info_intoVM(EarphoneInfo* data);
void rdx_vm_read_ep_info_fromVM(void);
#endif


#ifdef __cplusplus
}
#endif

#endif // _RDX_VM_H_
