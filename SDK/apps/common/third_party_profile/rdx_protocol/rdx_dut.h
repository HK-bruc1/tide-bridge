/*=====================================================================================
 HEADER NAME: rdx_dut.h
 MODULE NAME: DUT (Device Under Test) application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	DUT mode functions for factory testing (PIN project).
    Ported from BJ Tide project, adapted for PIN hardware (LED + motor, no OLED).
    
    PIN DUT测试项目:
    - 单击: LED灯光测试 (ft_oled / 兼容命名)
    - 双击: 马达震动测试 (ft_motor)
    - 三击: 录音测试     (ft_rec)
    - 四击: WiFi测试      (ft_wifi)
    - 五击: 格式化存储     (ft_format)
    - 六击: 关机          (ft_poweroff)
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: auto-ported from BJ Tide project
 Date: 2026-02-24
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_dut.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_DUT_H__
#define __RDX_DUT_H__ 

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* Include files
******************************************************************************/ 
#include "system/includes.h"
#include <stdbool.h>

/******************************************************************************
* Macro Define Section - DUT测试指令定义
******************************************************************************/ 
#define FT_DUT                  "ft_dut"
#define FT_OLED                 "ft_oled"
#define FT_MOTOR                "ft_motor"
#define FT_REC                  "ft_rec"
#define FT_WIFI                 "ft_wifi"
#define FT_FORMAT               "ft_format"
#define FT_POWEROFF             "ft_poweroff"
#define FT_FINALPACK_END        "ft_finalpack_end"
#define FT_KEY_DUT_ENABLE       "ft_key_dut_enable"

#define KEY_DUT_DISABLED_FLAG       (0xAA)

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
typedef enum {
    DUT_FUNC_NONE = 0,
    DUT_FUNC_OLED,
    DUT_FUNC_MOTOR,
    DUT_FUNC_REC,
    DUT_FUNC_WIFI,
    DUT_FUNC_FORMAT,
    DUT_FUNC_MAX
} rdx_dut_func_e;

typedef struct {
    bool dut_mode;
    bool key_dut_disabled;
    rdx_dut_func_e current_func;

    u16 motor_timer;
    bool motor_run;
} rdx_dut_info_t;

/******************************************************************************
* Function Section - DUT基础接口
******************************************************************************/ 
void rdx_dut_init(void);
rdx_dut_info_t* rdx_dut_get_info(void);

bool rdx_dut_is_in_mode(void);
void rdx_dut_close_current_func(void);

/******************************************************************************
* Function Section - DUT测试功能接口
******************************************************************************/ 
void rdx_dut_oled_start(void);
void rdx_dut_oled_stop(void);
bool rdx_dut_oled_is_running(void);

void rdx_dut_motor_start(void);
void rdx_dut_motor_stop(void);
bool rdx_dut_motor_is_running(void);

void rdx_dut_rec_start(void);
void rdx_dut_rec_stop(void);
bool rdx_dut_rec_is_running(void);

void rdx_dut_wifi_start(void);
void rdx_dut_wifi_stop(void);
bool rdx_dut_wifi_is_running(void);

void rdx_dut_format_start(void);

void rdx_dut_poweroff(void);

void rdx_dut_finalpack_end(void);
void rdx_dut_key_dut_enable(void);
bool rdx_dut_is_key_dut_disabled(void);
void rdx_dut_finalpack_end_format_cb(u8 result);

/******************************************************************************
* Function Section - BLE指令处理
******************************************************************************/ 
void rdx_dut_ble_cmd_handle(const char* cmd, const char* value);

/******************************************************************************
* Function Section - 按键处理
******************************************************************************/ 
void rdx_dut_key_handle(int key_msg);

/******************************************************************************
* Function Section - DUT消息处理
******************************************************************************/ 
void rdx_dut_msg_handle(void);

/******************************************************************************
* Function Section - DUT显示处理
******************************************************************************/ 
void rdx_dut_show_refresh(void);


#ifdef __cplusplus
}
#endif

#endif
