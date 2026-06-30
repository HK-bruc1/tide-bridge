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
    WIFI_AP_SSID_SUFFIX_NONE       = 0, /* ��׷�Ӻ�׺, SSID = ap_ssid           */
    WIFI_AP_SSID_SUFFIX_MAC_TAIL3  = 1, /* ap_ssid_<MACĩ3�ֽ�hex>  �� Octic_C4F3D5 */
    WIFI_AP_SSID_SUFFIX_AUTH_TAIL4 = 2, /* ap_ssid_<auth SNĩ4λ>  �� Octic_M3MU    */
}WifiApSsidSuffixMode;

typedef struct {
    const char *ap_ssid;            /* ���� SSID �� (e.g. "Octic")              */
    const char *ap_password;        /* ��̬����; ��̬����ʧ��ʱ����ʹ��         */
    u8          dynamic_psw_enable; /* 1=SHA256(auth+MAC)��̬�����Ұ� suffix ƴ��
                                       0=SSID/����ֱ�������������ַ���������    */
    u8          ssid_suffix_mode;   /* �� WifiApSsidSuffixMode                  */
}RdxWifiCfg;

void xxp_uart_register_wifi_cfg(const RdxWifiCfg *cfg);

#endif/*__XXPUART_H__*/

