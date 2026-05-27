#ifndef _ICSD_COMMON_V2_APP_H_
#define _ICSD_COMMON_V2_APP_H_

#include "generic/typedef.h"
#include "icsd_common_v2.h"

/*AFQ COMMON MSG List*/
enum {
    AFQ_COMMON_MSG_RUN = 0xA1,
};

enum {
    ANC_EXT_OPEN_FAIL_REENTRY = 0x1,	//重入
    ANC_EXT_OPEN_FAIL_FUNC_CONFLICT,	//功能冲突
    ANC_EXT_OPEN_FAIL_CFG_MISS,			//参数缺失
};

//频响获取选择
enum audio_adaptive_fre_sel {
    AUDIO_ADAPTIVE_FRE_SEL_AFQ = 0,
    AUDIO_ADAPTIVE_FRE_SEL_ANC,
};

//APP存储频响结构
struct audio_afq_output {
    u8 state;
    __afq_data sz_l;
    __afq_data sz_r;
};

struct audio_afq_bulk {
    struct list_head entry;
    const char *name;
    void (*output)(struct audio_afq_output *p);
};

int audio_afq_common_init(void);
int audio_afq_common_del_output_handler(const char *name);
int audio_afq_common_add_output_handler(const char *name, int data_sel, void (*output)(struct audio_afq_output *p));
int audio_afq_common_output_post_msg(__afq_output *out);
//判断AFQ APP应用是否活动
u8 audio_afq_common_app_is_active(void);

#endif
