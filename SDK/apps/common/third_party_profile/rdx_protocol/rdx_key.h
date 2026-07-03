/*=====================================================================================
 HEADER NAME: rdx_key.h
 MODULE NAME: rdx key application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-04-13 10:02:45
 LastEditors: sheng.dong
 LastEditTime: 2025-04-13 10:02:52
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_key.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_KEY__
#define __RDX_KEY__


/******************************************************************************
* Include files
******************************************************************************/ 
#include <stdbool.h>
#include "gpio.h"
#include "key_driver.h"
#include "app_msg.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/


/******************************************************************************
* Global Variables Section
******************************************************************************/ 
extern u8 key_table_idle_l[KEY_ACTION_MAX];
extern u8 key_table_idle_r[KEY_ACTION_MAX];

extern u8 key_table_normal_l[KEY_ACTION_MAX];
extern u8 key_table_normal_r[KEY_ACTION_MAX];

extern u8 key_table_music_l[KEY_ACTION_MAX];
extern u8 key_table_music_r[KEY_ACTION_MAX];

extern u8 key_table_calling_l[KEY_ACTION_MAX];
extern u8 key_table_calling_r[KEY_ACTION_MAX];

extern u8 key_table_call_active_l[KEY_ACTION_MAX];
extern u8 key_table_call_active_r[KEY_ACTION_MAX];

extern u8 key_table_recording_l[KEY_ACTION_MAX];
extern u8 key_table_recording_r[KEY_ACTION_MAX];

extern u8 key_table_dut_l[KEY_ACTION_MAX];
extern u8 key_table_dut_r[KEY_ACTION_MAX];

extern u8 key_table_ota_l[KEY_ACTION_MAX];
extern u8 key_table_ota_r[KEY_ACTION_MAX];

extern u8 key_table_incharge_l[KEY_ACTION_MAX];
extern u8 key_table_incharge_r[KEY_ACTION_MAX];

extern u8 key_table_wifi_l[KEY_ACTION_MAX];
extern u8 key_table_wifi_r[KEY_ACTION_MAX];


/******************************************************************************
* IO NUM key tables — 5 physical buttons (KEY_IO_NUM0~4)
******************************************************************************/
extern u8 key_table_io_num0_normal[KEY_ACTION_MAX];
extern u8 key_table_io_num1_normal[KEY_ACTION_MAX];
extern u8 key_table_io_num2_normal[KEY_ACTION_MAX];
extern u8 key_table_io_num3_normal[KEY_ACTION_MAX];
extern u8 key_table_io_num4_normal[KEY_ACTION_MAX];

/******************************************************************************
* Function Section
******************************************************************************/

u8 *rdx_key_get_io_num_table(int num_idx, int scene);
void rdx_key_io_num_log(int num_idx, int action);

#endif