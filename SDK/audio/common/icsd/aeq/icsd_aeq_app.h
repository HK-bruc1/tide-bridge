
#ifndef __ICSD_AEQ_APP_H_
#define __ICSD_AEQ_APP_H_

#include "typedef.h"
#include "icsd_aeq.h"
#include "app_config.h"
#include "effects/effects_adj.h"
#include "effects/eq_config.h"
#include "icsd_common_v2_app.h"

#define ADAPTIVE_EQ_TARGET_NODE_NAME 			"AEQ"			//自适应EQ目标节点名称
#define ADAPTIVE_EQ_VOLUME_NODE_NAME			"Vol_BtmMusic"	//自适应EQ依赖音量节点名称
#define ADAPTIVE_EQ_ORDER						10

#define ADAPTIVE_EQ_TARGET_DEFAULT_CFG_READ		0				//自适应EQ读取目标节点参数使能，如果AEQ没有独立节点，则必须读取

#define ADAPTIVE_EQ_VOLUME_GRADE_EN				1			//根据音量分档使能
#define ADAPTIVE_EQ_TIGHTNESS_GRADE_EN			1			//根据佩戴松紧度分档使能

#define ADAPTIVE_EQ_MAXGAIN_DB					6			//非音量/松紧度分档使用

#define ADAPTIVE_EQ_ONLY_IN_MUSIC_UPDATE		1			//(实时AEQ)仅在播歌/通话的时候更新

#if ADAPTIVE_EQ_TIGHTNESS_GRADE_EN && (!TCFG_AUDIO_FIT_DET_ENABLE)
#error "Must open TCFG_AUDIO_FIT_DET_ENABLE"
#endif


//ICSD AEQ 状态
enum {
    ADAPTIVE_EQ_STATE_CLOSE = 0,
    ADAPTIVE_EQ_STATE_OPEN,
    ADAPTIVE_EQ_STATE_RUN,
    ADAPTIVE_EQ_STATE_FORCE_EXIT,	//强制退出
};

//ICSD AEQ 滤波器模式
enum ADAPTIVE_EFF_MODE {
    AEQ_EFF_MODE_DEFAULT = 0,		//默认参数
    AEQ_EFF_MODE_ADAPTIVE,			//自适应参数
};

/*
   自适应EQ功能打开
	param: fre_sel  	训练数据来源
		   result_cb,	训练结束回调接口(如没有可传NULL)
	return 0  打开成功； 1 打开失败
*/
int audio_adaptive_eq_open(enum audio_adaptive_fre_sel fre_sel, void (*result_cb)(int result));

//(实时)自适应EQ打开
int audio_real_time_adaptive_eq_open(enum audio_adaptive_fre_sel fre_sel, void (*result_cb)(int result));

//(实时)自适应EQ退出
int audio_real_time_adaptive_eq_close(void);

int audio_adaptive_eq_close();

int audio_adaptive_eq_init(void);

//查询AEQ 算法是否运行中
u8 audio_adaptive_eq_is_running(void);

//查询AEQ 当前状态
u8 audio_adaptive_eq_state_get(void);

//读取自适应AEQ结果
struct eq_default_seg_tab *audio_icsd_adaptive_eq_read(void);

/*
   自适应EQ滤波器模式设置
   param: mode 0 普通参数； 1 自适应参数
*/
int audio_adaptive_eq_eff_set(enum ADAPTIVE_EFF_MODE mode);

int audio_adaptive_eq_vol_update(s16 volume);

int audio_adaptive_eq_process(void);

// (单次)自适应EQ强制退出
int audio_adaptive_eq_force_exit(void);

int audio_adaptive_eq_tool_data_get(u8 **buf, u32 *len);

int audio_adaptive_eq_tool_sz_data_get(u8 **buf, u32 *len);

#endif/*__ICSD_AEQ_APP_H_*/

