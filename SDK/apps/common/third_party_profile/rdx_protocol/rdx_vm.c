/*=====================================================================================
 HEADER NAME: rdx_vm.c
 MODULE NAME: rdx vm module.
 
 GENERAL DESCRIPTION: 	
 	This File handles all VM (non-volatile memory) operations for the RDX application.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2026-02-26
 LastEditors: sheng.dong
 LastEditTime: 2026-05-21 16:54:22
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_vm.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/
#include "app_config.h"
#include "system/includes.h"
#include "user_cfg.h"
#include "user_cfg_id.h"
#include "syscfg_id.h"
#include "btstack/avctp_user.h"
#include "le_common.h"
#include "bt_ble.h"
#include "bt_tws.h"
#include "log.h"

#include "rdx_app_config.h"
#include "rdx_vm.h"
#include "rdx_commonDef.h"
#include "rdx_ble_server.h"
#include "rdx_record.h"
#include "rdx_protocol.h"
#include "rdx_rtc.h"
#include "rdx_uxfile.h"
#include "rdx_util.h"

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_vm.data.bss")
#pragma data_seg(".rdx_vm.data")
#pragma const_seg(".rdx_vm.text.const")
#pragma code_seg(".rdx_vm.text")
#endif

/*******************************************************************************
* Macro Define Section
*******************************************************************************/
#define LOG_TAG                                             "[rdx_vm]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

#define RDX_LIC_PAGE_OFFSET                                 (80)

#define RDX_BOUND_STATE_UNBOUND                             (0)
#define RDX_BOUND_STATE_BOUND                               (1)

/*******************************************************************************
* Structure and Enum Section
*******************************************************************************/
typedef struct __flash_of_lic_para_head {
    s16 crc;
    u16 string_len;
    const u8 para_string[];
} __attribute__((packed)) _flash_of_lic_para_head;

typedef struct {
    char eq_info[11];
    char bt_name[LOCAL_NAME_LEN];
    u8 key_recored[2][6];
} rdx_vm_info_modify_t;

/*******************************************************************************
* Local variables Section
*******************************************************************************/
static rdx_bound_info_t rdx_bound_info = {
    .bound_state = 0
};

static rdx_vm_info_modify_t rdx_vm_info_modify;

static rdx_auth_info_t rdx_auth_info;
#if RDX_PRODUCT_IS_CHARGE_CASE
static EarphoneInfo epInfo;
#endif

static bool unbounding = FALSE;

/*******************************************************************************
* Function Declaration Section
******************************************************************************/
extern void sys_set_auto_off_time(u16 auto_off_time);
extern u8 get_ota_status();
extern RecordStatus* rdx_record_get_status();
extern void rdx_record_mic_gain_set_default();
extern int rdx_ble_server_reset_local_name();
extern u32 sdfile_get_disk_capacity(void);
extern u32 sdfile_flash_addr2cpu_addr(u32 offset);
extern void rdx_protocol_choose_to_unbound_ack_indicate(u8 result, u8 is_bound);
extern void rdx_protocol_bound_result_indicate(u8 result);
extern int rdx_uxfile_sd_format(uxfile_format_cb formatCB);
extern void rdx_app_earphone_pack_readchardata(void);
extern void rdx_app_time_to_reset(void);
extern void rdx_record_err_reboot_flag_write_into_vm(u8 flag);
extern ReqFileInfo* rdx_protocol_get_uploadfileInfo(void);
#if (TCFG_USER_TWS_ENABLE)
extern void bt_tws_remove_pairs(void);
#endif
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
extern void rdx_app_reset_AI_mode_info(void);
#endif
#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)
extern void rdx_ble_server_app_disconnect(void);
#endif

static bool rdx_vm_license_para_head_check(u8 *para);

/*******************************************************************************
* Function Section
******************************************************************************/

/**************************************************************************
 * function: rdx_vm_init
 * description: 初始化VM模块
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_vm_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_bound_info.bound_state = 0;
    
    memset(&rdx_vm_info_modify, 0, sizeof(rdx_vm_info_modify_t));
    
    memset(&rdx_auth_info, 0, sizeof(rdx_auth_info_t));
    
    y_printf("rdx_vm_init completed\r");
}

/**************************************************************************
 * function: rdx_vm_get_bound_status
 * description: 获取设备绑定状态
 * param (*)
 * return u8 - 当前绑定状态
 **************************************************************************/
u8 rdx_vm_get_bound_status(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    syscfg_read(VM_RDX_NOTTA_BOUND_STATUS, &rdx_bound_info.bound_state, 1);
    g_printf("rdx_vm_get_bound_status: %d\r", rdx_bound_info.bound_state);

    return rdx_bound_info.bound_state;
}

/**************************************************************************
 * function: rdx_vm_set_bound_status
 * description: 设置设备绑定状态
 * param (u8) d - 要设置的绑定状态
 * param (bool) show_en - 是否显示状态
 * return (*)
 **************************************************************************/
void rdx_vm_set_bound_status(u8 d, u8 show_en)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    g_printf("rdx_vm_set_bound_status :%d, is show: %d \n", d, show_en);

    //set value.
    rdx_bound_info.bound_state = d;
    syscfg_write(VM_RDX_NOTTA_BOUND_STATUS, &rdx_bound_info.bound_state, 1);

    if(show_en){
        // OLED 功能已删除
    }
}

/**************************************************************************
 * function: rdx_vm_bound_status_check
 * description: 检查并更新绑定状态
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_vm_bound_status_check(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_vm_get_bound_status();

#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    if(rdx_bound_info.bound_state == RDX_BOUND_STATE_BOUND){
BOUND);
    }else{
UNBOUND);
    }
#endif
}

/**************************************************************************
 * function: rdx_vm_is_unbouding
 * description: 查询是否正在解绑中
 * param (*)
 * return bool
 **************************************************************************/
u8 rdx_vm_is_unbouding(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return unbounding;
}

/**************************************************************************
 * function: rdx_vm_unbound_cb
 * description: 解绑回调函数
 * param (u8) result - 格式化结果
 * return (*)
 **************************************************************************/
void rdx_vm_unbound_cb(u8 result)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(result == MEM_FORMAT_RESULT_OK){
        y_printf("rdx_vm_unbound_cb --> format sd card ok! \r");
        //reset user para.
    #if TCFG_USER_TWS_ENABLE
        bt_tws_remove_pairs();
    #endif 
    #if (RDX_AI_TRANSLATE_SUPPORT == 1)
        rdx_app_reset_AI_mode_info();
    #endif
    #if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
    if(tws_api_get_role() == TWS_ROLE_MASTER){
        rdx_ble_server_app_disconnect();
    }
    #endif
        bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);

        //set default bt name.
        u8 name[LOCAL_NAME_LEN];
        memset(name, 0x00, sizeof(name));
        syscfg_read_string(CFG_BT_NAME, name, sizeof(name), 0);
        syscfg_write(CFG_BT_NAME, name, LOCAL_NAME_LEN);

        //set default ble name.
        rdx_ble_server_reset_local_name();

        //reset off time.
        sys_set_auto_off_time(RDX_DEFAULT_SHUT_DOWN_TIME);

        //mic gain set defalut.
        rdx_record_mic_gain_set_default();

        //set unbound, do not show bound status on oled.
        rdx_vm_set_bound_status(0, 0);

        rdx_protocol_bound_result_indicate(0);

        unbounding = false;

        os_time_dly(100);

        //DO system reset.
        rdx_cpu_reset();
    }else{
        rdx_protocol_bound_result_indicate(1);
        unbounding = false;
        y_printf("rdx_vm_unbound_cb --> format sd card fail! \r");
    }
}

/**************************************************************************
 * function: rdx_vm_unbound_handle
 * description: 处理解绑操作
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_vm_unbound_handle(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    unbounding = true;
    
    //format sd card.
    rdx_uxfile_sd_format(rdx_vm_unbound_cb);
}

/**************************************************************************
 * function: rdx_vm_choose_to_unbound_cb
 * description: 选择解绑回调函数
 * param (u8) result - 格式化结果
 * return (*)
 **************************************************************************/
void rdx_vm_choose_to_unbound_cb(u8 result)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(result == MEM_FORMAT_RESULT_OK){
        y_printf("rdx_vm_choose_to_unbound_cb --> format sd card ok! \r");
        
        //set unbound, do not show bound status on oled.
        rdx_vm_set_bound_status(0, 0);

        rdx_protocol_choose_to_unbound_ack_indicate(0, rdx_bound_info.bound_state);
        unbounding = false;

        os_time_dly(50);

        //DO system reset.
        rdx_cpu_reset();
    }else{
        y_printf("rdx_vm_choose_to_unbound_cb --> format sd card fail! \r");
        rdx_protocol_choose_to_unbound_ack_indicate(1, rdx_bound_info.bound_state);
        unbounding = false;
    }
}

/**************************************************************************
 * function: rdx_vm_choose_to_unbound_handle
 * description: 处理选择解绑操作
 * param (int) usr_para - 用户参数
 * param (int) format_en - 格式化使能
 * return (*)
 **************************************************************************/
void rdx_vm_choose_to_unbound_handle(int usr_para, int format_en)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    unbounding = true;

    if(usr_para == 1){
        bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);

        //set default bt name.
        u8 name[LOCAL_NAME_LEN];
        memset(name, 0x00, sizeof(name));
        syscfg_read_string(CFG_BT_NAME, name, sizeof(name), 0);
        syscfg_write(CFG_BT_NAME, name, LOCAL_NAME_LEN);

        //set default ble name.
        rdx_ble_server_reset_local_name();

        //reset off time.
        sys_set_auto_off_time(RDX_DEFAULT_SHUT_DOWN_TIME);

        //mic gain set defalut.
        rdx_record_mic_gain_set_default();

    }
    if(format_en == 1){
        rdx_protocol_choose_to_unbound_ack_indicate(0, rdx_bound_info.bound_state);
        rdx_uxfile_sd_format(rdx_vm_choose_to_unbound_cb);
    }else{
        //set unbound, do not show bound status on oled.
        rdx_vm_set_bound_status(0, 0);

        rdx_protocol_choose_to_unbound_ack_indicate(0, rdx_bound_info.bound_state);
        unbounding = false;

        os_time_dly(100);

        //DO system reset.
        rdx_cpu_reset();
    }
}

/**************************************************************************
 * function: rdx_vm_license_para_head_check
 * description: 检查许可证参数头
 * param (u8) *para
 * return (*)
 **************************************************************************/
static bool rdx_vm_license_para_head_check(u8 *para)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    _flash_of_lic_para_head *head;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    //fill head
    head = (_flash_of_lic_para_head *)para;

    ///crc check
    u8 *crc_data = (u8 *)(para + sizeof(((_flash_of_lic_para_head *)0)->crc));
    u32 crc_len = sizeof(_flash_of_lic_para_head) - sizeof(((_flash_of_lic_para_head *)0)->crc)/*head crc*/ + (head->string_len)/*content crc,include end character '\0'*/;
    s16 crc_sum = 0;

    crc_sum = CRC16(crc_data, crc_len);

    if (crc_sum != head->crc) {
        y_printf("license crc error !!! %x %x \n", (u32)crc_sum, (u32)head->crc);
        return false;
    }

    return true;
}

/**************************************************************************
 * function: rdx_vm_get_license_ptr
 * description: 获取许可证指针
 * param (*)
 * return (*)
 **************************************************************************/
const u8 *rdx_vm_get_license_ptr(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u32 flash_capacity = sdfile_get_disk_capacity();
    u32 flash_addr = flash_capacity - 256 + RDX_LIC_PAGE_OFFSET;
    u8 *lic_ptr = NULL;
    _flash_of_lic_para_head *head;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("flash capacity:%x \r", flash_capacity);
    lic_ptr = (u8 *)sdfile_flash_addr2cpu_addr(flash_addr);

    //head length check
    head = (_flash_of_lic_para_head *)lic_ptr;
    if (head->string_len >= 0xff) {
        y_printf("license length error !!! \n");
        return NULL;
    }

    ////crc check
    if (rdx_vm_license_para_head_check(lic_ptr) == (false)) {
        y_printf("license head check fail\n");
        return NULL;
    }

    put_buf(lic_ptr, 128);

    lic_ptr += sizeof(_flash_of_lic_para_head);
    return lic_ptr;
}

/**************************************************************************
 * function: rdx_vm_read_product_info_from_flash
 * description: 从flash读取产品信息
 * param (u8) *read_buf
 * param (u16) buflen
 * return (*)
 **************************************************************************/
u8 rdx_vm_read_product_info_from_flash(u8 *read_buf, u16 buflen)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 *rp = read_buf;
    const u8 *rdx_ptr = (u8 *)rdx_vm_get_license_ptr();    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("-------- rdx_ptr: 0x%x \r", rdx_ptr);
    // put_buf(rdx_ptr, buflen);

    if (rdx_ptr == NULL) {
        y_printf("%s -->rdx_ptr == NULL \r", __FUNCTION__);
        return FALSE;
    }
    memcpy(rp, rdx_ptr, buflen);
    return TRUE;
}

/**************************************************************************
 * function: rdx_vm_auth_info_init
 * description: 初始化认证信息
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_vm_auth_info_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    // 最大长度为 T251012505251164620375E2,1ECA35412AB3,01A012530A000001 + '\0'
    int flash_ret = 0;
    char temp[RDX_FACTORY_AUTHENTICATOR_TOTAL_SIZE + 1]; 
    char *auth_key_part = NULL;
    char *mac_addr_part = NULL;
    char *label_sn_part = NULL;
    u8 mac_hex[6];
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    b_printf("---------- rdx_vm_auth_info_init \r");
    memset(rdx_auth_info.AuthKey, 0, RDX_BLE_DEVICE_AUTH_KEY_SIZE + 1);
    memset(temp, 0, RDX_FACTORY_AUTHENTICATOR_TOTAL_SIZE + 1);

#if 1
    flash_ret = rdx_vm_read_product_info_from_flash(temp, sizeof(temp) - 1);
#else
    //for test. 模拟读取数据
    flash_ret = TRUE;
    memcpy(temp, "MCMEMOAA2025031800004045,50EE407836FD,01A012530A000001", strlen("MCMEMOAA2025031800004045,50EE407836FD,01A012530A000001")); 
#endif
    
    if (flash_ret == TRUE){
        g_printf("---- read flash success --> temp len: %d, temp: %s \r", strlen(temp), temp);

        u8 buffer[RDX_FACTORY_AUTHENTICATOR_TOTAL_SIZE];
        memset(buffer, 0, RDX_FACTORY_AUTHENTICATOR_TOTAL_SIZE);
        memcpy(buffer, temp, strlen(temp));

        // 使用 strtok 分割字符串
        auth_key_part = strtok(temp, ",");
        mac_addr_part = strtok(NULL, ",");
        label_sn_part = strtok(NULL, ",");

        if (auth_key_part != NULL){
            y_printf("%s --> auth_key_part len: %d \r", __FUNCTION__, strlen(auth_key_part));
            if(strlen(auth_key_part) >= RDX_BLE_DEVICE_AUTH_KEY_SIZE) {
                strncpy(rdx_auth_info.AuthKey, auth_key_part, RDX_BLE_DEVICE_AUTH_KEY_SIZE);
            }else if(strlen(auth_key_part) <= 16){
                //tuya 无Mac
                strncpy(rdx_auth_info.AuthKey, buffer, RDX_BLE_DEVICE_AUTH_KEY_SIZE);
                y_printf("%s --> auth key \r", __FUNCTION__);
                rdx_auth_info.AuthKey[RDX_BLE_DEVICE_AUTH_KEY_SIZE] = '\0';
                put_buf(rdx_auth_info.AuthKey, 25);
                g_printf("----- read auth data from flash success, auth key: %s \n", rdx_auth_info.AuthKey);
                rdx_app_earphone_pack_readchardata();
                return;
            }
            rdx_auth_info.AuthKey[RDX_BLE_DEVICE_AUTH_KEY_SIZE] = '\0';
            put_buf(rdx_auth_info.AuthKey, 25);
            g_printf("----- read auth data from flash success, auth key: %s \n", rdx_auth_info.AuthKey);
        } else {
            r_printf("---------- invalid rdx_auth_info.AuthKey format or length exceeds limit \n");
        }
        char t_buf[50];
        memset(t_buf, 0, 50);
        if (mac_addr_part != NULL) {
            strncpy(t_buf, mac_addr_part, RDX_BLE_MAC_STRING_SIZE);
            y_printf("%s --> t_buf: %s \n", __FUNCTION__, t_buf);
            u32 ret = rdx_util_str_hexstr2hexarray((u8 *)t_buf, strlen(t_buf), mac_hex);
            if (ret == 0) {
                rdx_util_reverse_byte(mac_hex, 6);
                memset(rdx_auth_info.ble_mac_hex, 0, 6);
                memcpy(rdx_auth_info.ble_mac_hex, mac_hex, 6);
                memset(rdx_auth_info.ble_mac_str, 0, RDX_BLE_MAC_STRING_SIZE + 1);
                strncpy(rdx_auth_info.ble_mac_str, t_buf, 12);
                le_controller_set_mac((u8 *)mac_hex);
                b_printf("===> %02X:%02X:%02X:%02X:%02X:%02X \r", mac_hex[5], mac_hex[4], mac_hex[3], mac_hex[2], mac_hex[1], mac_hex[0]);
                put_buf((u8 *)mac_hex, 6);
            }
        } else {
            r_printf("---------- invalid Mac address format or length exceeds limit \n");
        }
        if (label_sn_part != NULL) {
            memset(rdx_auth_info.label_sn, 0, RDX_LABEL_SN_SIZE + 1);
            strncpy((char *)rdx_auth_info.label_sn, label_sn_part, RDX_LABEL_SN_SIZE);
            b_printf("---------- rdx_auth_info.label_sn: %s \n", rdx_auth_info.label_sn);
        }else{
            r_printf("---------- invalid label_sn format or length exceeds limit \n");
        }
    }
    //readchardata packed after ep_info loaded in rdx_app_all_init.
}

/**************************************************************************
 * function: rdx_vm_get_auth_info
 * description: 获取认证信息结构体指针
 * param (*)
 * return rdx_auth_info_t* - 认证信息结构体指针
 **************************************************************************/
rdx_auth_info_t* rdx_vm_get_auth_info(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return &rdx_auth_info;
}

#if RDX_PRODUCT_IS_CHARGE_CASE
/**************************************************************************
 * function: rdx_vm_write_ep_info_intoVM
 * description: Write earphone (charge case 配对) info into VM_RDX_CUSTOM_AUTH
 * param (EarphoneInfo*) data
 * return (int) TRUE = ok, FALSE = fail
 **************************************************************************/
int rdx_vm_write_ep_info_intoVM(EarphoneInfo* data)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    int vm_ep_result = syscfg_write(VM_RDX_CUSTOM_AUTH, data, sizeof(EarphoneInfo));
    y_printf("=== write earphone info to vm, result: %d\r", vm_ep_result);
    if(vm_ep_result <= 0){
        r_printf("---------- write earphone info fail \r");
        return FALSE;
    }
    return TRUE;
}

/**************************************************************************
 * function: rdx_vm_read_ep_info_fromVM
 * description: Read earphone (charge case 配对) info from VM_RDX_CUSTOM_AUTH
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_vm_read_ep_info_fromVM(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    int vm_case_result = syscfg_read(VM_RDX_CUSTOM_AUTH, &epInfo, sizeof(EarphoneInfo));
    y_printf("=== read ep info from vm, result: %d\r", vm_case_result);
    if(vm_case_result <= 0){
        r_printf("---------- read ep info fail \r");
        memset(&epInfo, 0, sizeof(EarphoneInfo));
    }else{
        y_printf("=== read ep info from vm success, mac: %s \r",
                 epInfo.ep_mac_str[0] == '\0' ? "000000000000" : epInfo.ep_mac_str);
    }
}

/**************************************************************************
 * function: rdx_vm_get_ep_info
 * description: 获取已配对耳机信息结构体指针 (RAM cache)
 * param (*)
 * return EarphoneInfo*
 **************************************************************************/
EarphoneInfo* rdx_vm_get_ep_info(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return &epInfo;
}
#endif /* RDX_PRODUCT_IS_CHARGE_CASE */

/**************************************************************************
 * function: rdx_vm_sys_reset_to_defaults
 * description: 系统重置为默认值
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_vm_sys_reset_to_defaults(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus *rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("====== %s ------> APP_MSG_BT_PAIR_SET_DEFAULT!!! \n", __FUNCTION__);
    // check if recording or ota
    if(get_ota_status()){
        return;
    }
    if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
        y_printf("\r =====%s --> command reject, now is recording or on ota \r", __func__);
        return;
    }
    //file transferring.
    ReqFileInfo* r_file = rdx_protocol_get_uploadfileInfo();
    if(r_file->file_send_busy == true){
        y_printf("\r =====%s --> command reject, now is file transferring \r", __func__);
        return;
    }

    //BT & TWS set default, do system restart.
#if TCFG_USER_TWS_ENABLE
    bt_tws_remove_pairs();
#endif 
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    rdx_app_reset_AI_mode_info();
#endif
#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
if(tws_api_get_role() == TWS_ROLE_MASTER){
    rdx_ble_server_app_disconnect();
}
#endif
    bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);

    //set default bt name.
    u8 name[LOCAL_NAME_LEN];
    memset(name, 0x00, sizeof(name));
    syscfg_read_string(CFG_BT_NAME, name, sizeof(name), 0);
    syscfg_write(CFG_BT_NAME, name, LOCAL_NAME_LEN);

    //set default ble name.
    rdx_ble_server_reset_local_name();

    //clear record error flag.
    rdx_record_err_reboot_flag_write_into_vm(0);

    //reset off time.
    sys_set_auto_off_time(RDX_DEFAULT_SHUT_DOWN_TIME);

    //mic gain set defalut.
    rdx_record_mic_gain_set_default();

#if (RDX_RTC_PATH_SEL == RDX_RTC_PATH_SOFTWARE)
    //store rtc timestamp for software path only, hardware path saved by poweroff uninitcall.
    rdx_rtc_store_timestamp();
#endif

    rdx_app_time_to_reset();     
}

/**
 * @description  : 用户参数设置为默认值
 * @param ()
 * @return ()
 */
void rdx_vm_user_para_set_defaults(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    RecordStatus *rp = rdx_record_get_status();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("====== %s ------> APP_MSG_BT_PAIR_SET_DEFAULT!!! \n", __FUNCTION__);
    // check if recording or ota
    if(get_ota_status()){
        return;
    }
    if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
        y_printf("\r =====%s --> command reject, now is recording or on ota \r", __func__);
        return;
    }
    //file transferring.
    ReqFileInfo* r_file = rdx_protocol_get_uploadfileInfo();
    if(r_file->file_send_busy == true){
        y_printf("\r =====%s --> command reject, now is file transferring \r", __func__);
        return;
    }

    //BT & TWS set default, do system restart.
#if TCFG_USER_TWS_ENABLE
    bt_tws_remove_pairs();
#endif 
#if (RDX_AI_TRANSLATE_SUPPORT == 1)
    rdx_app_reset_AI_mode_info();
#endif
#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN) 
if(tws_api_get_role() == TWS_ROLE_MASTER){
    rdx_ble_server_app_disconnect();
}
#endif
    bt_cmd_prepare(USER_CTRL_DEL_ALL_REMOTE_INFO, 0, NULL);

    //set default bt name.
    u8 name[LOCAL_NAME_LEN];
    memset(name, 0x00, sizeof(name));
    syscfg_read_string(CFG_BT_NAME, name, sizeof(name), 0);
    syscfg_write(CFG_BT_NAME, name, LOCAL_NAME_LEN);

    //set default ble name.
    rdx_ble_server_reset_local_name();

    //clear record error flag.
    rdx_record_err_reboot_flag_write_into_vm(0);

    //reset off time.
    sys_set_auto_off_time(RDX_DEFAULT_SHUT_DOWN_TIME);

    //mic gain set defalut.
    rdx_record_mic_gain_set_default();

#if (RDX_RTC_PATH_SEL == RDX_RTC_PATH_SOFTWARE)
    //store rtc timestamp for software path only, hardware path saved by poweroff uninitcall.
    rdx_rtc_store_timestamp();
#endif
}