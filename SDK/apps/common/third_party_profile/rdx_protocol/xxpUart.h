/*=====================================================================================
 HEADER NAME: .c
 MODULE NAME: application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-01-10 23:57:52
 LastEditors: sheng.dong
 LastEditTime: 2025-06-24 21:54:09
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\xxpUart.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __XXPUART_H__
#define __XXPUART_H__


/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define WIFI_UDP_LOCAL_IP                           "192.168.4.1"
#define WIFI_UDP_LOCAL_PORT                         (9527)

#define WIFI_UDP_DEST_IP                            "192.168.4.2"
#define WIFI_UDP_DEST_PORT                          (8936)


#define WIFI_TCP_LOCAL_IP                           "192.168.4.1"
#define WIFI_TCP_LOCAL_PORT                         (9527)

#define WIFI_POWER_PORT_IO							IO_PORTA_04
#define WIFI_POWER_PORT								PORTA
#define WIFI_POWER_PIN								PORT_PIN_4
#define WIFI_POWER_ON								gpio_set_mode(IO_PORT_SPILT(WIFI_POWER_PORT_IO), PORT_OUTPUT_HIGH)
#define WIFI_POWER_OFF								gpio_set_mode(IO_PORT_SPILT(WIFI_POWER_PORT_IO), PORT_HIGHZ)


/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 

typedef int (*rdx_ble_send_buff_callback)(int send_state);


enum xxp_mssg {
    XPP_MSSG_IRQ_CALLBACK = 0X10A0,
};

#define XPPDATANODE_LIST_SIZE sizeof(xppDataNodeList)

typedef struct __xppDataNodeList{
	void*  dBuff;
	int  dSize;
	struct __xppDataNodeList *next;
}xppDataNodeList;


typedef struct __xxpUart_st 
{
    int uart_id;
    unsigned int  (*wriite)(const void *buf, unsigned int len);
}xxpUart_st;

typedef struct{
    char ssid[64];
    char pswd[64];
    u8 mac_bytes[6];
    u8 ap_conn_dev_ip[4];
    u8 ap_conn_dev_ip_str[16];
}ApInfo;

typedef enum {
    WIFI_AP_SSID_SUFFIX_NONE       = 0, /* 不追加后缀, SSID = ap_ssid           */
    WIFI_AP_SSID_SUFFIX_MAC_TAIL3  = 1, /* ap_ssid_<MAC末3字节hex>  例 Octic_C4F3D5 */
    WIFI_AP_SSID_SUFFIX_AUTH_TAIL4 = 2, /* ap_ssid_<auth SN末4位>  例 Octic_M3MU    */
}WifiApSsidSuffixMode;

typedef struct {
    const char *ap_ssid;            /* 基础 SSID 名 (e.g. "Octic")              */
    const char *ap_password;        /* 静态密码; 动态密码失败时回退使用         */
    u8          dynamic_psw_enable; /* 1=SHA256(auth+MAC)动态密码且按 suffix 拼名
                                       0=SSID/密码直接用上面两个字符串字面量    */
    u8          ssid_suffix_mode;   /* 见 WifiApSsidSuffixMode                  */
}RdxWifiCfg;

void xxp_uart_register_wifi_cfg(const RdxWifiCfg *cfg);

#endif/*__XXPUART_H__*/

