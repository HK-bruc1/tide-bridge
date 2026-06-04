/*=====================================================================================
 HEADER NAME: rdx_key.c
 MODULE NAME: rdx key application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-04-13 10:01:12
 LastEditors: sheng.dong
 LastEditTime: 2025-04-13 10:01:32
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_key.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#include "app_msg.h"
#include "rdx_key.h"
#include "gpio_config.h"
#include "power/power_wakeup.h"
#include "rdx_app_config.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 

/******************************************************************************
* Global Variables Section
******************************************************************************/ 

#if (RDX_AI_SEL_APP & APP_TINGNAO_EN)
//idle.
u8 key_table_idle_l[KEY_ACTION_MAX] = {
    APP_MSG_NULL,  //短按
    APP_MSG_RDX_APP_WAKEUP,    //长按
    APP_MSG_NULL,    //hold
    APP_MSG_NULL,    //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL, 
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,  //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC  APP_MSG_RECORD_CHAT_MODE
    APP_MSG_NULL,  //长按5s KEY_ACTION_HOLD_5SEC
    APP_MSG_NULL,   //长按8s KEY_ACTION_HOLD_8SEC
    APP_MSG_NULL,  //长按10s KEY_ACTION_HOLD_10SEC
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL
};
u8 key_table_idle_r[KEY_ACTION_MAX] = {
    APP_MSG_NULL,  //短按
    APP_MSG_RDX_APP_WAKEUP,    //长按
    APP_MSG_NULL,    //hold
    APP_MSG_NULL,    //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL, 
    APP_MSG_NULL,  //五击
    APP_MSG_NULL, //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL, 
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC  APP_MSG_RECORD_CHAT_MODE
    APP_MSG_NULL,  //长按5s KEY_ACTION_HOLD_5SEC
    APP_MSG_NULL,   //长按8s KEY_ACTION_HOLD_8SEC
    APP_MSG_NULL,  //长按10s KEY_ACTION_HOLD_10SEC
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL
};
#else
//idle.
u8 key_table_idle_l[KEY_ACTION_MAX] = {
    APP_MSG_RDX_APP_WAKEUP,  //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,    //hold
    APP_MSG_NULL,    //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL, 
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,  //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC  APP_MSG_RECORD_CHAT_MODE
    APP_MSG_NULL,  //长按5s KEY_ACTION_HOLD_5SEC
    APP_MSG_NULL,   //长按8s KEY_ACTION_HOLD_8SEC
    APP_MSG_NULL,  //长按10s KEY_ACTION_HOLD_10SEC
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL
};
u8 key_table_idle_r[KEY_ACTION_MAX] = {
    APP_MSG_RDX_APP_WAKEUP,  //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,    //hold
    APP_MSG_NULL,    //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL, 
    APP_MSG_NULL,  //五击
    APP_MSG_NULL, //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL, 
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC  APP_MSG_RECORD_CHAT_MODE
    APP_MSG_NULL,  //长按5s KEY_ACTION_HOLD_5SEC
    APP_MSG_NULL,   //长按8s KEY_ACTION_HOLD_8SEC
    APP_MSG_NULL,  //长按10s KEY_ACTION_HOLD_10SEC
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL
};
#endif

//normal.
u8 key_table_normal_l[KEY_ACTION_MAX] = {
    APP_MSG_NULL,  //短按 (OLED 功能已删除)
    APP_MSG_RECORD_SWITCH,    //长按
    APP_MSG_NULL,    //hold
    APP_MSG_LONG_PRESS_HOLDUP,    //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL, 
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,  //六击
    APP_MSG_NULL, 
    APP_MSG_DUT,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC  APP_MSG_RECORD_CHAT_MODE
    APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
    APP_MSG_NULL,   //长按8s KEY_ACTION_HOLD_8SEC
    APP_MSG_NULL,  //长按10s KEY_ACTION_HOLD_10SEC
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL
};
u8 key_table_normal_r[KEY_ACTION_MAX] = {
    APP_MSG_NULL,  //短按 (OLED 功能已删除)
    APP_MSG_RECORD_SWITCH,    //长按
    APP_MSG_NULL,    //hold
    APP_MSG_LONG_PRESS_HOLDUP,    //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL, 
    APP_MSG_BT_PAIR_SET_DEFAULT,  //五击
    APP_MSG_NULL, //六击
    APP_MSG_NULL, 
    APP_MSG_DUT,
    APP_MSG_NULL, 
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC  APP_MSG_RECORD_CHAT_MODE
    APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
    APP_MSG_NULL,   //长按8s KEY_ACTION_HOLD_8SEC
    APP_MSG_NULL,  //长按10s KEY_ACTION_HOLD_10SEC
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL
};

//music.
u8 key_table_music_l[KEY_ACTION_MAX] = {
    APP_MSG_NULL,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,    //双击
    APP_MSG_NULL,    //三击
    APP_MSG_NULL,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC
};
u8 key_table_music_r[KEY_ACTION_MAX] = {
    APP_MSG_NULL,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,     //双击
    APP_MSG_NULL,    //三击
    APP_MSG_NULL,
    APP_MSG_NULL,    //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC
};

//calling.
u8 key_table_calling_l[KEY_ACTION_MAX] = {
    APP_MSG_NULL,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,     //双击
    APP_MSG_NULL,    //三击
    APP_MSG_NULL,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC
};
u8 key_table_calling_r[KEY_ACTION_MAX] = {
    APP_MSG_NULL,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,     //双击
    APP_MSG_NULL,    //三击
    APP_MSG_NULL,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC
};

//call active.
u8 key_table_call_active_l[KEY_ACTION_MAX] = {
    APP_MSG_NULL, //APP_MSG_RECORD_CALL_MODE,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,     //双击
};
u8 key_table_call_active_r[KEY_ACTION_MAX] = {
    APP_MSG_NULL, //APP_MSG_RECORD_CALL_MODE,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,     //双击
};

//recording.
u8 key_table_recording_l[KEY_ACTION_MAX] = {
    APP_MSG_SINGLE_CLICK, //短按 (V24: 录音中单击 → 插入录音标记, 见 rdx_app_single_click_handle)
    APP_MSG_RECORD_SWITCH,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_LONG_PRESS_HOLDUP,           //长按抬起
    APP_MSG_DOUBLE_CLICK,  //双击
    APP_MSG_TRIPLE_CLICK,  //三击
    APP_MSG_NULL,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,
    APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
};
u8 key_table_recording_r[KEY_ACTION_MAX] = {
    APP_MSG_SINGLE_CLICK, //短按 (V24: 录音中单击 → 插入录音标记, 见 rdx_app_single_click_handle)
    APP_MSG_RECORD_SWITCH,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_LONG_PRESS_HOLDUP,           //长按抬起
    APP_MSG_DOUBLE_CLICK,  //双击
    APP_MSG_TRIPLE_CLICK,  //三击
    APP_MSG_NULL,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,
    APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
};

//dut.
u8 key_table_dut_l[KEY_ACTION_MAX] = {
    APP_MSG_SINGLE_CLICK,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_DOUBLE_CLICK,  //双击
    APP_MSG_TRIPLE_CLICK,  //三击
    APP_MSG_QUADRUPLE_CLICK, //APP_MSG_QUADRUPLE_CLICK,
    APP_MSG_BT_PAIR_SET_DEFAULT,  //五击
    APP_MSG_NULL, //APP_MSG_SEXTUPLE_CLICK,   //六击
    APP_MSG_NULL, 
    APP_MSG_DUT,
    APP_MSG_NULL,  //index = 11
    APP_MSG_SEXTUPLE_CLICK,
    APP_MSG_NULL, //APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
};
u8 key_table_dut_r[KEY_ACTION_MAX] = {
    APP_MSG_SINGLE_CLICK,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_DOUBLE_CLICK,  //双击
    APP_MSG_TRIPLE_CLICK,  //三击
    APP_MSG_QUADRUPLE_CLICK, //APP_MSG_QUADRUPLE_CLICK,
    APP_MSG_BT_PAIR_SET_DEFAULT,  //五击
    APP_MSG_NULL, //APP_MSG_SEXTUPLE_CLICK,   //六击
    APP_MSG_NULL, 
    APP_MSG_DUT,
    APP_MSG_NULL,  //index = 11
    APP_MSG_SEXTUPLE_CLICK,
    APP_MSG_NULL, //APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
};

//ota.
u8 key_table_ota_l[KEY_ACTION_MAX] = {
    APP_MSG_SINGLE_CLICK,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,
    APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
};
u8 key_table_ota_r[KEY_ACTION_MAX] = {
    APP_MSG_SINGLE_CLICK,       //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,
    APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
};

//incharge.
u8 key_table_incharge_l[KEY_ACTION_MAX] = {
    APP_MSG_NULL,       //短按 (OLED 功能已删除)
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,
    APP_MSG_NULL,  //长按5s KEY_ACTION_HOLD_5SEC
};
u8 key_table_incharge_r[KEY_ACTION_MAX] = {
    APP_MSG_NULL,       //短按 (OLED 功能已删除)
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,           //hold
    APP_MSG_NULL,           //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_NULL,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,   //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,
    APP_MSG_NULL,  //长按5s KEY_ACTION_HOLD_5SEC
};

//wifi.
u8 key_table_wifi_l[KEY_ACTION_MAX] = {
    APP_MSG_SINGLE_CLICK,  //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,    //hold
    APP_MSG_NULL,    //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_QUADRUPLE_CLICK, //APP_MSG_QUADRUPLE_CLICK,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL,  //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL,  //index = 10
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC  APP_MSG_RECORD_CHAT_MODE
    APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
    APP_MSG_NULL,   //长按8s KEY_ACTION_HOLD_8SEC
    APP_MSG_NULL,  //长按10s KEY_ACTION_HOLD_10SEC
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL
};
u8 key_table_wifi_r[KEY_ACTION_MAX] = {
    APP_MSG_SINGLE_CLICK,  //短按
    APP_MSG_NULL,    //长按
    APP_MSG_NULL,    //hold
    APP_MSG_NULL,    //长按抬起
    APP_MSG_NULL,  //双击
    APP_MSG_NULL,  //三击
    APP_MSG_QUADRUPLE_CLICK, //APP_MSG_QUADRUPLE_CLICK,
    APP_MSG_NULL,  //五击
    APP_MSG_NULL, //六击
    APP_MSG_NULL, 
    APP_MSG_NULL,
    APP_MSG_NULL, 
    APP_MSG_NULL,  //长按3s KEY_ACTION_HOLD_3SEC  APP_MSG_RECORD_CHAT_MODE
    APP_MSG_POWER_OFF_READY,  //长按5s KEY_ACTION_HOLD_5SEC
    APP_MSG_NULL,   //长按8s KEY_ACTION_HOLD_8SEC
    APP_MSG_NULL,  //长按10s KEY_ACTION_HOLD_10SEC
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL, 
    APP_MSG_NULL
};

/******************************************************************************
* Local Variables Section
******************************************************************************/ 

/******************************************************************************
* Function Declaration Section
******************************************************************************/

/******************************************************************************
* Function Section
******************************************************************************/ 


