/*=====================================================================================
 HEADER NAME: rdx_ota.c
 MODULE NAME: rdx ota application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-10-16 22:52:10
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 22:52:13
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_ota.c
 
 Self-documenting Code
=====================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_ota.data.bss")
#pragma data_seg(".rdx_ota.data")
#pragma const_seg(".rdx_ota.text.const")
#pragma code_seg(".rdx_ota.text")
#endif

/******************************************************************************
* Include files
******************************************************************************/ 
#include "stdlib.h"
#include "sdk_config.h"
#include "app_msg.h"
#include "earphone.h"
#include "app_main.h"
#include "circular_buf.h"

#include "rdx_ota.h"
#include "os/os_type.h"
#include "os/os_api.h"
#include "dual_bank_updata_api.h"
#include "update_loader_download.h"
#include "update_tws_new.h"
#include "timer.h"
#include "clock.h"
#include "clock_manager/clock_manager.h"
#include "classic/tws_api.h"
#include "update_tws_new.h"
#include "btstack/avctp_user.h"

#include "rdx_app_config.h"
#include "rdx_commonDef.h"
#include "rdx_util.h"
#include "rdx_protocol.h"
#include "rdx_ble_server.h"
#include "driver/device/usb/usb.h"

#include "rdx_uxfile.h"
#include "rdx_led_ctrl.h"
#include "rdx_default_hooks.h"
#include "rdx_jl_osal.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define LOG_TAG                                 "[rdx_ota]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"



#define OTA_WRITE_FLASH_SIZE						(1024)

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
typedef struct {
    u32 file_size;
    u32 recv_len;
    u32 file_crc;
    u16 rdx_ota_packet_num;
    u8  buff[OTA_WRITE_FLASH_SIZE];
    u32 buff_size;
} rdx_ota_t;

typedef struct{
	u32 version;
	u32 file_size;
	u32 pack_total;
	u16 pack_each;
    u8  state;
    u32 cur_pack_num;
    u8  buff[OTA_WRITE_FLASH_SIZE];
    u32 buff_size;
}OTA_UpgradePara;

/******************************************************************************
* Global Variables Section
******************************************************************************/ 


/******************************************************************************
* Local Variables Section
******************************************************************************/ 
static int old_sys_clk;
rdx_ota_t rdx_ota;

OTA_UpgradePara otaPara;

static u16 rdx_ota_get_data_timer = 0; //ota data response timer
static u32 pack_cnt = 0; //for ota data transfer timeout timer rerun count.

/******************************************************************************
* Function Section
******************************************************************************/ 
extern void rdx_post_key_event(u8 type);
extern int tws_ota_data_send_pend(void);
extern int rdx_get_ble_mtu_size(void);
// extern u8 dual_bank_update_verify_without_crc_new(int (*verify_result_hdl)(int calc_crc));
extern u8 dual_bank_update_verify_without_crc(int (*verify_result_hdl)(int calc_crc));
extern u32 webusb_tx_data(const usb_dev usb_id, const u8 *buffer, u32 len);
extern const usb_dev rdx_webusb_get_usb_id(void);
extern u16 rdx_ble_server_get_conn_handle(void);

void rdx_ota_stop(void);

/**************************************************************************
 * function: _rdx_ota_split_params
 * description: 
 * param (char) *text
 * param (char) *params
 * param (int) len
 * param (u8) range
 * param (u8*) size
 * return (*)
 **************************************************************************/
static int _rdx_ota_split_params(char *text, char *params[], int len, u8 range, u8* size)
{
	/*----------------------------------------------------------------*/
	/* Local Variables												  */
	/*----------------------------------------------------------------*/
	char *p = text;
	char *c = NULL;
	char *s = NULL;
	int i = 0;
    int j = 0;
	//int len = sizeof(params) / sizeof(params[0]);
	/*----------------------------------------------------------------*/
	/* Code Body													  */
	/*----------------------------------------------------------------*/
    log_info("_rdx_ota_split_params --> len = %d, range = %d \r", len, range);
    for(j = 0; j < range; j++){
		c = strchr(p,'#');
		if(!c){
			return -1;
		}	
		if(c == p){
			log_info("[COMMAND]--->_command_split_params:bad format.");
			return -1;
		}
		s = (char *)MALLOC(c - p + 1);
		if(!s){
			log_info("[COMMAND]--->_command_split_params:no memory.");
			return 1;
		}
		memset(s, 0, c - p + 1);
		memcpy(s, p, c - p);
		params[i] = s;
		i ++;
		//array is full?
		if(i == len){
            if(size){
                *size = c - text + 1;
                // b_printf("%s --> size = %d \r", __func__, *size);
            }
            break;
        }
		p = c + 1;
	}
	return 0;
}

/**************************************************************************
 * function: 
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
void rdx_ota_reset(void *priv)
{
	/*----------------------------------------------------------------*/
	/* Local Variables                                                */
	/*----------------------------------------------------------------*/

	/*----------------------------------------------------------------*/
	/* Code Body                                                      */
	/*----------------------------------------------------------------*/
    log_info("\ncpu_reset!");
    rdx_cpu_reset();
}

/**************************************************************************
 * function: rdx_ota_update_write_cb
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
int rdx_ota_update_write_cb(void *priv)
{
	/*----------------------------------------------------------------*/
	/* Local Variables                                                */
	/*----------------------------------------------------------------*/
	
	/*----------------------------------------------------------------*/
	/* Code Body                                                      */
	/*----------------------------------------------------------------*/
    
    return 0;
}

void rdx_ota_end_disconnect(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_ble_server_app_disconnect();
    rdx_os_timer_add(rdx_ota_reset, NULL, 500);
}

/**************************************************************************
 * function: rdx_ota_boot_info_cb
 * description: 
 * param (int) err
 * return (*)
 **************************************************************************/
int rdx_ota_boot_info_cb(int err)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("rdx_ota_boot_info_cb:%d", err);
#if (OTA_TWS_SAME_TIME_ENABLE)
    extern int tws_ota_result(u8 err);
    tws_ota_result(err);
#else

    if (err == 0) {
        rdx_ota_data_response_and_request();
        rdx_os_timer_add(rdx_ota_reset, NULL, 1000);
    }
#endif
    return 0;
}

/**************************************************************************
 * function: rdx_ota_clk_resume
 * description: 
 * param (int) priv
 * return (*)
 **************************************************************************/
int rdx_ota_clk_resume(int priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    clock_free("sys");     //恢复时钟
    return 0;
}

/**************************************************************************
 * function: rdx_ota_data_response_request
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_ota_data_response_and_request(void)
{
	/*----------------------------------------------------------------*/
	/* Local Variables                                                */
	/*----------------------------------------------------------------*/
    char buf[100];
    u16 len = strlen(CMD_UP_OTA_DATA_REQ);
	/*----------------------------------------------------------------*/
	/* Code Body                                                      */
	/*----------------------------------------------------------------*/
    memset(buf, 0, 100);
    strncpy(buf, CMD_UP_OTA_DATA_REQ, len);
    sprintf(buf + len, "%d#%d#", otaPara.state, otaPara.cur_pack_num);
    len = strlen(buf);
    rdx_ble_server_ota_send((u8*)buf, len);
    return 0;
}

/**************************************************************************
 * function: rdx_ota_file_end_response
 * description: 
 * param (void) *priv
 * return (*)
 **************************************************************************/
int rdx_ota_file_end_response(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 res, up_flg;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
#if (OTA_TWS_SAME_TIME_ENABLE)
    // 把最后一次OTA_DATA的信号量pend到再开始校验
    if (tws_ota_data_send_pend()) {
        log_info("last ota data pend error");
        otaPara.state = 2;
        goto TUYA_VERIFY_END;
    }
#endif

    old_sys_clk = clk_get("sys");
#if (OTA_TWS_SAME_TIME_ENABLE)
    u8 rsp_data = OTA_TWS_VERIFY_WITHOUT_CRC;
    if (tws_api_get_tws_state() & TWS_STA_SIBLING_DISCONNECTED) {
        log_info("tws_disconn in verify\n");
        db_update_notify_fail_to_phone();
        otaPara.state = 3;
        goto TUYA_VERIFY_END;
    }
    if (tws_ota_trans_to_sibling(&rsp_data, 1)) { //让从机也开始校验，而不是等主机校验完从机再校验，节约时间
        otaPara.state = 3;
        goto TUYA_VERIFY_END;
    }
#else
    u8 rsp_data = 0;
#endif
    log_info("old_sys_clk:%d", old_sys_clk);
    if (160 * 1000000L - old_sys_clk > 0) {
        clock_alloc("sys", 160 * 1000000L - old_sys_clk);     //提升系统时钟提高校验速度
    }
    if (dual_bank_update_verify_without_crc(rdx_ota_clk_resume) == 0) {
        log_info("UPDATE SUCCESS");
#if (OTA_TWS_SAME_TIME_ENABLE)
        if (tws_ota_enter_verify_without_crc(NULL)) {
            log_info("SIBLING VERIFY_WITHOUT_CRC ERROR");
            otaPara.state = 3;
            goto TUYA_VERIFY_END;
        } else {
            log_info("SIBLING VERIFY_SUCCESS");
            otaPara.state = 0;
        }
        if (!tws_ota_exit_verify(&res, &up_flg)) {
            log_info("SIBLING BURN_BOOT ERROR");
            otaPara.state = 3;
        } else {
            log_info("SIBLING BURN_BOOT SUCCESS");
            otaPara.state = 0;
        }
        otaPara.cur_pack_num = 0;
#else
        otaPara.state = 0;
        otaPara.cur_pack_num = 0;
#endif
        
        dual_bank_update_burn_boot_info(rdx_ota_boot_info_cb);
    #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED || RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
        // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_SHUTOFF);  
    #endif
    } else {
        log_info("UPDATE FAILURE");
        otaPara.state = 2;
        otaPara.cur_pack_num = 0;
        //do deal with the failure.

        //ble disconnect.
        rdx_ble_server_app_disconnect();

        //do restart.
        rdx_os_timer_add(rdx_ota_reset, NULL, 2000);
    }

TUYA_VERIFY_END:
    otaPara.cur_pack_num = 0;
    
    return rdx_ota_data_response_and_request();
}

/**************************************************************************
 * function: rdx_ota_end
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ota_end(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    g_printf("============ Pend the second last ota data sibling rsp \r");
#if (OTA_TWS_SAME_TIME_ENABLE)
    tws_ota_data_send_pend();
#endif
    r_printf("====== RDX_BLE_OTA_END ====== \n");
    if (otaPara.buff_size != 0) {          //把剩余的数据写入flash
#if (OTA_TWS_SAME_TIME_ENABLE)
        if (tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED) {
            tws_ota_data_send_m_to_s(otaPara.buff, otaPara.buff_size);
        } else {
            log_info("TWS DISCONNECT, OTA ABORT!");
            otaPara.state = RDX_OTA_DATA_OTHER_ERR;
            otaPara.cur_pack_num = 0;           
            rdx_ota_data_response_and_request();
        }
#endif
        u32 ret = dual_bank_update_write(otaPara.buff, otaPara.buff_size, rdx_ota_file_end_response);
        if(ret != 0){
            r_printf("final write err! \n");
            rdx_ota_file_end_response(NULL);
        }
    } else {
        rdx_ota_file_end_response(NULL);
    }
}

void rdx_ota_get_data_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (rdx_ota_get_data_timer) {
        rdx_os_timer_del(rdx_ota_get_data_timer);
        rdx_ota_get_data_timer = 0;
    }
}

/**************************************************************************
 * function: rdx_ota_get_data_timeout_cb
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
static void rdx_ota_get_data_timeout_cb(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    //close ota.
    log_info("rdx_ota_get_data_timeout_cb --> timeout, close ota");

    //do stop ota.
    rdx_ota_stop();

#if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED || RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
    // OLED 功能已删除 // os_taskq_post_msg("oled_show_task", 1, OLED_SHOW_SHUTOFF);  
#endif    
}

/**************************************************************************
 * function: rdx_ota_get_data_timer_rerun
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ota_get_data_timer_rerun(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rdx_ota_get_data_timer) {
        rdx_os_timer_re_run(rdx_ota_get_data_timer);
    }
}

/**************************************************************************
 * function: rdx_ota_get_data_timer_start
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ota_get_data_timer_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("rdx_ota_get_data_timer_start --> rdx_ota_get_data_timer: %d \r", rdx_ota_get_data_timer);
    if(rdx_ota_get_data_timer) {
        rdx_os_timer_re_run(rdx_ota_get_data_timer);
    } else {
        rdx_ota_get_data_timer = rdx_os_timer_add_to_task("app_core", rdx_ota_get_data_timeout_cb, NULL, 10 * 1000);
    }
}

/**************************************************************************
 * function: rdx_ota_upgrade_cmd_ack
 * description: 
 * param (u8) result
 * return (*)
 **************************************************************************/
int rdx_ota_upgrade_cmd_ack(u8 result)
{
	/*----------------------------------------------------------------*/
	/* Local Variables                                                */
	/*----------------------------------------------------------------*/
    char buf[100];
    u16 len = strlen(CMD_UP_UPGRADE);
    u16 con_hdl = rdx_ble_server_get_conn_handle();
    usb_dev usb_id = rdx_webusb_get_usb_id();
	/*----------------------------------------------------------------*/
	/* Code Body                                                      */
	/*----------------------------------------------------------------*/
    //
    if (false == app_in_mode(APP_MODE_PC)) {
        return 0;
    }
    memset(buf, 0, 100);
    strncpy(buf, CMD_UP_UPGRADE, len);
    sprintf(buf + len, "%d#", result);
    len = strlen(buf);

    if (true == app_in_mode(APP_MODE_PC)) {
        u8* data_copy = NULL;
        data_copy = (u8*)malloc(len + 1);
        if (data_copy == NULL) {
            r_printf("%s --> malloc failed \r", __FUNCTION__);
            return 0;
        }
        memset(data_copy, 0, len + 1);
        memcpy(data_copy, buf, len);
        b_printf("===> %s --> send len: %d, data: %s \n", __FUNCTION__, len, data_copy);
        os_taskq_post_msg("webusb_send_task", 3, data_copy, len, 1);
        return 1;
    }else{
        r_printf("===> %s --> len = %d \n", __func__, len);
        rdx_ble_server_ota_send((u8*)buf, len);
    }    
    return 0;
}

/**************************************************************************
 * function: rdx_ota_upgrade_cmd_handler
 * description: 
 * param (u8*) d
 * param (u32) len
 * return (*)
 **************************************************************************/
int rdx_ota_upgrade_cmd_handler(u8* d, u32 len)
{
	/*----------------------------------------------------------------*/
	/* Local Variables                                                */
	/*----------------------------------------------------------------*/
    u8 *p = d;
	int filesize = 0;
    int total = 0;
    int per = 0;
	char ver_str[10];
    int i = 0;
    char *items[4];
    u16 temp_len = 0;
    bool ok = FALSE;
    u32 fw_version = 0;
	/*----------------------------------------------------------------*/
	/* Code Body                                                      */
	/*----------------------------------------------------------------*/
    b_printf("===> %s \r", d);

    //ota init.
    memset(&otaPara, 0, sizeof(OTA_UpgradePara));

	//3.6.8#512#128#4096#
    //find '#'.
    i = _rdx_ota_split_params((char*)p, items, 4, 25, NULL);
	if(0 != i)
		return i;

	memset(ver_str, 0, sizeof(ver_str));
    strcpy(ver_str, items[0]);
    ASSIGN_INT(filesize, items[1]);
    ASSIGN_INT(per, items[2]);
	ASSIGN_INT(total, items[3]);

    ok = TRUE;
	for(i = 0;i < 4;i ++){
		FREEIF(items[i]);
	}
	
	//check version.
    temp_len = strlen(ver_str);
    fw_version = rdx_util_version_str2int(ver_str, temp_len);
	//get total file size.
    otaPara.file_size = filesize;
	//get total pack number.
    otaPara.pack_total = total;
	//get each pack size.
    otaPara.pack_each = per;
    y_printf("%s --> fw_version = %08X, filesize = %d, per = %d, total = %d \r", __func__, fw_version, otaPara.file_size, otaPara.pack_each, otaPara.pack_total);

#if (OTA_TWS_SAME_TIME_ENABLE)
    if (tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED) {
        struct __tws_ota_para ota_para;
        ota_para.fm_crc = 0;
        ota_para.fm_size = otaPara.file_size;
        ota_para.max_pkt_len = OTA_WRITE_FLASH_SIZE;
        ota_para.param_len = 0;
        ota_para.param = NULL;
        int res = tws_ota_open(&ota_para);
        r_printf(">>>>>> tws_ota_open --> res = %d \n", res);
    }
#endif
    set_ota_status(1);
    r_printf(">>>>>> set_ota_status: %d \n", get_ota_status());
    dual_bank_passive_update_init(0, otaPara.file_size, OTA_WRITE_FLASH_SIZE, NULL);
    u32 r = dual_bank_update_allow_check(otaPara.file_size);
    b_printf("dual_bank_update_allow_check: r = %d, otaPara.file_size = %d \r", r, otaPara.file_size);
    if (fw_version <= FIRMWARE_VERSION_HEX) {
        r_printf("%s --> The current version is newer than the one issued!!! \r", __func__);
    #if (OTA_TWS_SAME_TIME_ENABLE)
        tws_ota_close();
    #endif
        dual_bank_passive_update_exit(NULL);
        set_ota_status(0);
        log_info("ota remote version lower than current version, ota abort");
        otaPara.state = RDX_OTA_STATE_VER_LOW;
        otaPara.cur_pack_num = 0;
    }else{
        if (dual_bank_update_allow_check(otaPara.file_size) != 0) {
    #if (OTA_TWS_SAME_TIME_ENABLE)
            tws_ota_close();
    #endif
            dual_bank_passive_update_exit(NULL);
            set_ota_status(0);
            otaPara.state = RDX_OTA_STATE_FILE_SIZE_TOO_LARGE;
            otaPara.cur_pack_num = 0;
        } else {
            //ok to update.
            g_printf("===== %s --> ok to upgrade! \n", __func__);
            otaPara.state = RDX_OTA_STATE_NORMAL;
            otaPara.cur_pack_num = 1;
            // OTA 升级开始，设置 OTA 灯效
            rdx_hook_led_set_scene(RDX_LED_SCENE_OTA_START);
            //start timer to check ota status.
            pack_cnt = 0;
            rdx_ota_get_data_timer_start();
        }
    }
    b_printf("RDX_BLE_OTA_FILE_INFO--> state = %d, cur_pack_num = %d, total = %d \r", otaPara.state, otaPara.cur_pack_num, otaPara.pack_total);
    
    memset(otaPara.buff, 0, OTA_WRITE_FLASH_SIZE);
    
    //send upgrade ack and ota data request.
    rdx_ota_data_response_and_request();

	return E_PROTOCOL_ECODE_SUCCESS;
}

/**************************************************************************
 * function: rdx_ota_get_data_handler
 * description: 
 * param (u8*) d
 * param (u32) len
 * return (*)
 **************************************************************************/
int rdx_ota_get_data_handler(u8* d, u32 len)
{
	/*----------------------------------------------------------------*/
	/* Local Variables                                                */
	/*----------------------------------------------------------------*/
    u8 *p = d;
	int pack_num = 0;
    int pack_size = 0;
    long long verify_value = 0;
    int i = 0;
    char *items[3];
    bool ok = FALSE;
    u8 size = 0;
    u16 temp_len = 0;
    u8 protocol_ver = rdx_protocol_package_verify_method();
    int verify_res = -1;    
	/*----------------------------------------------------------------*/
	/* Code Body                                                      */
	/*----------------------------------------------------------------*/
	//16#128#sum#xxxxxxxxxxxxxxx…
	// b_printf("===> %s --> len = %d \r", __func__, len);
	//find '#'.
	i = _rdx_ota_split_params((char*)p, items, 3, 50, &size);
	if(0 != i)
		return i;

	ASSIGN_INT(pack_num, items[0]);
	ASSIGN_INT(pack_size, items[1]);
	// ASSIGN_LONG_INT(verify_value, items[2]);
	char *endptr;
	verify_value = strtoll(items[2], &endptr, 10);
	// g_printf("===> sum: %u, items[2]: %s \r", verify_value, items[2]);

	for(i = 0;i < 3;i ++){
		FREEIF(items[i]);
	}

    r_printf("%s --> pack_num = %d, pack_size = %d, verify_value = %lu\r", __func__, pack_num, pack_size, verify_value);

    //check got pack number ok?
    if(pack_num != otaPara.cur_pack_num){
        log_info("%s --> pack num = %d, need num = %d, get pack number error! \r", __func__, otaPara.cur_pack_num);
        otaPara.state = RDX_OTA_DATA_PKT_NUM_ERR;
        otaPara.cur_pack_num = 0;
        EXCEPTION_THROW();
    }
    
    //do verify.
    if(protocol_ver == RDX_PACKAGE_VERIFY_CHECKSUM){
        u8 cal_sum = 0;
        cal_sum = rdx_util_check_sum8(p + size, pack_size);
        log_info("%s --> get num = %d, cal_sum = %d \r", __func__, verify_value, cal_sum);
        if(cal_sum == verify_value){
            verify_res = 0;
        }
    }else{
        u32 cal_crc = 0;
        cal_crc = rdx_util_crc32(p + size, pack_size, &cal_crc);
        log_info("%s --> get sum = %u, cal_crc = %lu \r", __func__, verify_value, cal_crc);
        if(cal_crc == verify_value){
            verify_res = 0;
        }
    }
    if(verify_res != 0){
        log_info("%s --> verify error! \r", __func__);
        otaPara.state = RDX_OTA_DATA_CRC_FAILED;
        otaPara.cur_pack_num = 0;
        EXCEPTION_THROW();
    }

    log_info("%s --> otaPara.buff_size = %d \r", __func__, otaPara.buff_size);
    if(otaPara.buff_size + pack_size >= OTA_WRITE_FLASH_SIZE){
        temp_len = OTA_WRITE_FLASH_SIZE - otaPara.buff_size;
        memcpy(otaPara.buff + otaPara.buff_size, p + size, temp_len);
#if (OTA_TWS_SAME_TIME_ENABLE)
        if (tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED) {
            tws_ota_data_send_m_to_s(otaPara.buff, OTA_WRITE_FLASH_SIZE);
        } else {
            log_info("===== TWS DISCONNECT, OTA ABORT!");
            otaPara.state = RDX_OTA_DATA_OTHER_ERR;
            otaPara.cur_pack_num = 0;
            memset(otaPara.buff, 0, OTA_WRITE_FLASH_SIZE);
            otaPara.buff_size = 0;  
            ok = FALSE;
            EXCEPTION_THROW();
        }
#endif
        dual_bank_update_write(otaPara.buff, OTA_WRITE_FLASH_SIZE, rdx_ota_update_write_cb);
        memset(otaPara.buff, 0, OTA_WRITE_FLASH_SIZE);
        otaPara.buff_size = 0;
        //get pack content.
        memcpy(otaPara.buff + otaPara.buff_size, p + size + temp_len, pack_size - temp_len);    
        otaPara.buff_size += pack_size - temp_len;
    }else{
        //get pack content.
        memcpy(otaPara.buff + otaPara.buff_size, p + size, pack_size);
        otaPara.buff_size += pack_size;
    }
    //is end pack.
    log_info("%s --> cur_pack_num = %d, pack_total = %d \r", __func__, otaPara.cur_pack_num, otaPara.pack_total);
    if(otaPara.cur_pack_num == otaPara.pack_total){
        //ota end handle.
        rdx_ota_end();
        //stop timer.
        rdx_ota_get_data_timer_stop();
        pack_cnt = 0;
        return E_PROTOCOL_ECODE_SUCCESS;
    }else{
        otaPara.cur_pack_num ++;
        if(pack_cnt > 10){
            //10 packs received data, reset pack count.
            pack_cnt = 0;
            rdx_ota_get_data_timer_rerun();
        }else{
            pack_cnt ++;
        }
    }
    ok = TRUE;

EXCEPTION_POINTER()
    rdx_ota_data_response_and_request();

	return ok ? E_PROTOCOL_ECODE_SUCCESS : E_PROTOCOL_ECODE_FAIL;
}

/**************************************************************************
 * function: rdx_ota_proc
 * description: 
 * param (u16) type
 * param (u8) *recv_data
 * param (u32) recv_len
 * return (*)
 **************************************************************************/
void rdx_ota_proc(u16 type, u8 *recv_data, u32 recv_len)
{
    /*----------------------------------------------------------------*/
	/* Local Variables                                                */
	/*----------------------------------------------------------------*/
    ReqFileInfo* rf_info = rdx_protocol_get_uploadfileInfo();
	/*----------------------------------------------------------------*/
	/* Code Body                                                      */
	/*----------------------------------------------------------------*/
    y_printf("%s --> recv data len: %d \r", __func__, recv_len); 
	switch (type){
		case OTA_UPGRADE_BEGIN:
			{
                rdx_ota_get_data_timer_start();
				rdx_ota_upgrade_cmd_handler(recv_data, recv_len);
			}
			break;

		case OTA_DATA_DL:
			{
				rdx_ota_get_data_handler(recv_data, recv_len);
			}
			break;

		default:
			break;
	}

}

/**************************************************************************
 * function: rdx_ota_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_ota_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("ota stop \r");
    rdx_ota_get_data_timer_stop();
    pack_cnt = 0;

    dual_bank_passive_update_exit(NULL);
    set_ota_status(0);
    
    // OTA 结束，恢复 BLE 广播灯效
    rdx_hook_led_set_scene(RDX_LED_SCENE_OTA_STOP);
    
    otaPara.state = RDX_OTA_STATE_NORMAL;
    otaPara.cur_pack_num = 0;
    memset(&otaPara, 0, sizeof(OTA_UpgradePara));
}

void rdx_ota_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    log_info("ota init \r");
    memset(&otaPara, 0, sizeof(OTA_UpgradePara));
    rdx_ota_get_data_timer = 0;
    pack_cnt = 0;
}

/**************************************************************************
 * function: rdx_app_ota_event_handler
 * description: 
 * param (int) *msg
 * return (*)
 **************************************************************************/
static int rdx_app_ota_event_handler(int *msg)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
#if (OTA_TWS_SAME_TIME_ENABLE)
    bt_ota_event_handler(msg);
#endif
    return 0;
}

APP_MSG_HANDLER(rdx_app_ota_msg_entry) = {
    .owner      = 0xff,
    .from       = MSG_FROM_OTA,
    .handler    = rdx_app_ota_event_handler,
};

#endif
