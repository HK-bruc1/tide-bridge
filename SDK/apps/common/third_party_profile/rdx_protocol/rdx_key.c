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
    APP_MSG_NULL,    //长按 [DEBUG: PB1 stuck test]
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
    APP_MSG_NULL,  //长按5s KEY_ACTION_HOLD_5SEC [DEBUG: PB1 stuck test]
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

// ============================================================
// IO NUM 映射表 — 5 个物理 IO 键 (KEY_IO_NUM0~4)
// 格式与业务按键表对齐: 每个 KEY_ACTION 位置一行 + 中文注释
// 短按抬起后经多击判定生成 CLICK/DOUBLE_CLICK；
// 持续按下生成 LONG/HOLD，长按抬起生成 UP
// ============================================================

/* RDX 在线就绪时，KEY5 长按开始在线录音流，抬起停止。 */
u8 key_table_record_hold[KEY_ACTION_MAX] = {
    APP_MSG_NULL,              // CLICK
    APP_MSG_RECORD_HOLD_START, // LONG
    APP_MSG_NULL,              // HOLD
    APP_MSG_RECORD_HOLD_STOP,  // UP
    APP_MSG_NULL,              // DOUBLE_CLICK
    APP_MSG_NULL,              // TRIPLE_CLICK
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
};

// KEY_IO_NUM0: 单击上一条录音文件，长按快退；双击不分配动作
u8 key_table_io_num0_normal[KEY_ACTION_MAX] = {
    APP_MSG_REC_PREV,          //短按
    APP_MSG_REC_FR,            //长按 (LONG: 快退)
    APP_MSG_NULL,              //hold
    APP_MSG_NULL,              //长按抬起
    APP_MSG_NULL,              //双击
    APP_MSG_NULL,              //三击
    APP_MSG_NULL,
    APP_MSG_NULL,              //五击
    APP_MSG_NULL,              //六击
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,              //index = 11
    APP_MSG_NULL,              //长按3s
    APP_MSG_NULL,              //长按5s
    APP_MSG_NULL,              //长按8s
    APP_MSG_NULL,              //长按10s
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
};

// KEY_IO_NUM1: 单击下一条录音文件，长按快进；双击不分配动作
u8 key_table_io_num1_normal[KEY_ACTION_MAX] = {
    APP_MSG_REC_NEXT,          //短按
    APP_MSG_REC_FF,            //长按 (LONG: 快进)
    APP_MSG_NULL,              //hold
    APP_MSG_NULL,              //长按抬起
    APP_MSG_NULL,              //双击
    APP_MSG_NULL,              //三击
    APP_MSG_NULL,
    APP_MSG_NULL,              //五击
    APP_MSG_NULL,              //六击
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,              //index = 11
    APP_MSG_NULL,              //长按3s
    APP_MSG_NULL,              //长按5s
    APP_MSG_NULL,              //长按8s
    APP_MSG_NULL,              //长按10s
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
};

// KEY_IO_NUM2: 单击音量加
u8 key_table_io_num2_normal[KEY_ACTION_MAX] = {
    APP_MSG_VOL_UP,            //短按
    APP_MSG_NULL,              //长按
    APP_MSG_NULL,              //hold
    APP_MSG_NULL,              //长按抬起
    APP_MSG_NULL,              //双击
    APP_MSG_NULL,              //三击
    APP_MSG_NULL,
    APP_MSG_NULL,              //五击
    APP_MSG_NULL,              //六击
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,              //index = 11
    APP_MSG_NULL,              //长按3s
    APP_MSG_NULL,              //长按5s
    APP_MSG_NULL,              //长按8s
    APP_MSG_NULL,              //长按10s
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
};

// KEY_IO_NUM3: 单击音量减
u8 key_table_io_num3_normal[KEY_ACTION_MAX] = {
    APP_MSG_VOL_DOWN,          //短按
    APP_MSG_NULL,              //长按
    APP_MSG_NULL,              //hold
    APP_MSG_NULL,              //长按抬起
    APP_MSG_NULL,              //双击
    APP_MSG_NULL,              //三击
    APP_MSG_NULL,
    APP_MSG_NULL,              //五击
    APP_MSG_NULL,              //六击
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,              //index = 11
    APP_MSG_NULL,              //长按3s
    APP_MSG_NULL,              //长按5s
    APP_MSG_NULL,              //长按8s
    APP_MSG_NULL,              //长按10s
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
};

// KEY_IO_NUM4: 离线单击切换播放/暂停，长按抬起后切换本地录音
// 长按触发链: LONG → APP_MSG_RECORD_SWITCH → flag=1，UP → APP_MSG_LONG_PRESS_HOLDUP → 切换录音
u8 key_table_io_num4_normal[KEY_ACTION_MAX] = {
    APP_MSG_REC_PLAY_TOGGLE,   //短按 (离线播放/暂停切换)
    APP_MSG_RECORD_SWITCH,     //长按 (LONG: 录音开关)
    APP_MSG_NULL,              //hold
    APP_MSG_LONG_PRESS_HOLDUP, //长按抬起 (UP: 释放后真正触发录音)
    APP_MSG_NULL,              //双击
    APP_MSG_NULL,              //三击
    APP_MSG_NULL,
    APP_MSG_NULL,              //五击
    APP_MSG_NULL,              //六击
    APP_MSG_NULL,
    APP_MSG_DUT,
    APP_MSG_NULL,              //index = 11
    APP_MSG_NULL,              //长按3s
    APP_MSG_NULL,              //长按5s
    APP_MSG_NULL,              //长按8s
    APP_MSG_NULL,              //长按10s
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
};

/* KEY5 在 DUT 场景下保留八击退出，普通业务动作保持禁用。 */
static u8 key_table_io_num4_dut[KEY_ACTION_MAX] = {
    APP_MSG_NULL,              // 单击
    APP_MSG_NULL,              // 长按
    APP_MSG_NULL,              // 持续按住
    APP_MSG_NULL,              // 长按抬起
    APP_MSG_NULL,              // 双击
    APP_MSG_NULL,              // 三击
    APP_MSG_NULL,              // 四击
    APP_MSG_NULL,              // 五击
    APP_MSG_NULL,              // 六击
    APP_MSG_NULL,              // 七击
    APP_MSG_DUT,               // 八击退出 DUT
    APP_MSG_NULL,
    APP_MSG_NULL,              // 长按3秒
    APP_MSG_NULL,              // 长按5秒
    APP_MSG_NULL,              // 长按8秒
    APP_MSG_NULL,              // 长按10秒
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
    APP_MSG_NULL,
};

static u8 *g_num_normal_tables[] = {
	key_table_io_num0_normal,
	key_table_io_num1_normal,
	key_table_io_num2_normal,
	key_table_io_num3_normal,
	key_table_io_num4_normal,
};

u8 *rdx_key_get_io_num_table(int num_idx, int scene)
{
	if (num_idx < 0 || num_idx > 4) {
		return NULL;
	}
	// scene: 0=IDLE, 1=NORMAL, 2=RECORDING, 3=WIFI, 4=DUT, 5=OTA
	switch (scene) {
	case 4:  // DUT — KEY5 保留八击退出
		return num_idx == 4 ? key_table_io_num4_dut : NULL;
	case 5:  // OTA — 物理按键不应干扰升级
		return NULL;
	default:
		return g_num_normal_tables[num_idx];
	}
}

// DEBUG: print which IO NUM key triggered
void rdx_key_io_num_log(int num_idx, int action)
{
	y_printf("\n ====== rdx_key_io_num_log: num_idx=%d, action=%d \r", num_idx+1, action);
}

/******************************************************************************
* Local Variables Section
******************************************************************************/

/******************************************************************************
* Function Declaration Section
******************************************************************************/

/******************************************************************************
* Function Section
******************************************************************************/
