/*=====================================================================================
 HEADER NAME: rdx_spp.c
 MODULE NAME: rdx spp ctrl module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-09-24 20:34:31
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 14:33:13
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_spp.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/
#include "sdk_config.h"
#include "app_msg.h"
#include "earphone.h"
#include "app_main.h"
#include "bt_tws.h"
#include "btstack/avctp_user.h"
#include "multi_protocol_main.h"
#include "rdx_spp.h"
#include "rdx_app.h"
#include "rdx_protocol.h"

/*******************************************************************************
* Macro Define Section
*******************************************************************************/
#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)


#define LOG_TAG             "[rdx_spp]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

/*******************************************************************************
* Structure and Enum Section
*******************************************************************************/


/******************************************************************************
* Global variable Section
******************************************************************************/


/*******************************************************************************
* Local variables Section
*******************************************************************************/
void *rdx_spp_hdl = NULL;


/*******************************************************************************
* Function Section
*******************************************************************************/

/**************************************************************************
 * function: rdx_spp_state_callback
 * description: 
 * param (void) *hdl
 * param (void) *remote_addr
 * param (u8) state
 * return (*)
 **************************************************************************/
static void rdx_spp_state_callback(void *hdl, void *remote_addr, u8 state)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int i;
    int bond_flag = 0;   
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    switch (state) {
    case SPP_USER_ST_CONNECT:
        y_printf("======== rdx spp connect#########\n");
        // 将 rdx_spp_hdl 绑定到连接上的设备地址，否则后续会收到所有已连接设备地址的事件和数据
        app_spp_set_filter_remote_addr(rdx_spp_hdl, remote_addr);
        break;

    case SPP_USER_ST_DISCONN:
        y_printf("======== custom spp disconnect#########\n");
        break;
    };
}

/**************************************************************************
 * function: rdx_spp_auth_sn_indicate
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
static void rdx_spp_auth_sn_indicate(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    char temp[100];
    u8 len = 0;
    u8 total = 0;
    DevBaseInfo* p = rdx_app_get_dev_base_info();
    char dest_sn[24];
    bool sn_empty = false;
    bool ble_mac_empty = false;
    bool wifi_mac_empty = false;
    bool label_sn_empty = false;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    //pack message.
    memset(temp, 0, 100);
    len = strlen("dev_info-->");
    strncpy(temp, "dev_info-->", len);
    total += len;
 
    memset(dest_sn, 0, 24);

#if 1
    // Check if p->sn is empty or all zeros
    if (p->auth == NULL || memcmp(p->auth, dest_sn, 24) == 0) {
        sn_empty = true;
    }

    // Check if p->ble_mac_str is empty or all zeros
    if (p->ble_mac_str == NULL || strlen(p->ble_mac_str) == 0) {
        ble_mac_empty = true;
    }

    // Check if p->wifi_mac_str is empty or all zeros
    if (p->wifi_mac_str == NULL || strlen(p->wifi_mac_str) == 0) {
        wifi_mac_empty = true;
    }

    if (p->label_sn == NULL || strlen(p->label_sn) == 0) {
        label_sn_empty = true;
    }

    // Pack the message with appropriate values
    // sprintf(temp + total, "%s,%s,%s\r", 
    //         sn_empty ? "000000000000000000000000" : p->auth, 
    //         ble_mac_empty ? "000000000000" : p->ble_mac_str, 
    //         wifi_mac_empty ? "000000000000" : p->wifi_mac_str);
    
    sprintf(temp + total, "%s#%s#%s#%s#", 
            sn_empty ? "000000000000000000000000" : p->auth, 
            ble_mac_empty ? "000000000000" : p->ble_mac_str, 
            wifi_mac_empty ? "000000000000" : p->wifi_mac_str,
            label_sn_empty ? "0000000000000000" : p->label_sn);

    total = strlen(temp);
#else
    //test.
    sprintf(temp + total, "%s#%s#%s#%s#", "MCMEMOAA2025031800000034", "15FFF4579451", "000000000000", "000000000000");
    total = strlen(temp);
#endif

    g_printf("%s --> total: %d, string: %s\r\n", __func__, total, temp);

    rdx_spp_send(temp, total);
}

/**************************************************************************
 * function: rdx_spp_version_indicate
 * description: 
 * param (char*) hv
 * param (char*) sv
 * return (*)
 **************************************************************************/
static void rdx_spp_version_indicate(char* hv, char* sv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    char temp[100];
    u8 len = 0;
    u8 total = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    //pack message.
    memset(temp, 0, 100);
    len = strlen("dev_version-->");
    strncpy(temp, "dev_version-->", len);
    total += len;

    sprintf(temp + total, "%s#%s#", hv, sv);
    total = strlen(temp);

    g_printf("%s --> total: %d, string: %s \r\n", __func__, total, temp);

    rdx_spp_send(temp, total);
}

/**************************************************************************
 * function: rdx_spp_recieve_callback
 * description: 
 * param (void) *hdl
 * param (void) *remote_addr
 * param (u8) *buf
 * param (u16) len
 * return (*)
 **************************************************************************/
static void rdx_spp_recieve_callback(void *hdl, void *remote_addr, u8 *buf, u16 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("rdx_spp_recieve_callback len=%d\n", len);
    // put_buf(buf, len);

    // // test send
    // rdx_spp_send(buf, len);

    //parse data. 
    char* p = NULL;
 
    p = strstr(buf, "<dev_info>");
    if(p){
        rdx_spp_auth_sn_indicate();
        return;
    }

    p = strstr(buf, "<dev_version>");
    if(p){
        rdx_spp_version_indicate(rdx_protocol_get_hardware_version(), rdx_protocol_get_firmware_version());
        return;
    }
}

/**************************************************************************
 * function: rdx_spp_send
 * description: 
 * param (u8) *data
 * param (u32) len
 * return (*)
 **************************************************************************/
int rdx_spp_send(u8 *data, u32 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return app_spp_data_send(rdx_spp_hdl, data, len);
}

/**************************************************************************
 * function: rdx_spp_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_spp_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("====== %s\n", __func__);
    if (rdx_spp_hdl == NULL) {
        rdx_spp_hdl = app_spp_hdl_alloc(0x0);
        if (rdx_spp_hdl == NULL) {
            y_printf("rdx_spp_hdl alloc err !\n");
            return;
        }
        app_spp_recieve_callback_register(rdx_spp_hdl, rdx_spp_recieve_callback);
        app_spp_state_callback_register(rdx_spp_hdl, rdx_spp_state_callback);
        app_spp_wakeup_callback_register(rdx_spp_hdl, NULL);
    }
}

/**************************************************************************
 * function: rdx_spp_exit
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_spp_exit(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    y_printf("%s\n", __func__);

    // do SPP exit
    if (NULL != app_spp_get_hdl_remote_addr(rdx_spp_hdl)) {
        app_spp_disconnect(rdx_spp_hdl);
    }
    app_spp_hdl_free(rdx_spp_hdl);
    rdx_spp_hdl = NULL;
}


#endif