#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".audio_pdm_demo.data.bss")
#pragma data_seg(".audio_pdm_demo.data")
#pragma const_seg(".audio_pdm_demo.text.const")
#pragma code_seg(".audio_pdm_demo.text")
#endif
/*
 ****************************************************************************
 *							Audio PDM Demo
 *
 *Description	: Audio PDM使用范例。PDM Link是一种数字MIC接口。
 *Notes			: 本demo为开发测试范例，请不要修改该demo， 如有需求，请自行
 *				  复制再修改
 ****************************************************************************
 */
#include "audio_demo.h"

#include "system/includes.h"
#include "cpu/includes.h"
#include "gpio.h"
#include "audio_dac.h"
#include "media/includes.h"
#include "audio_pdm.h"
#include "audio_config.h"

/* #define PLNK_DEMO_SR 		    16000//48000//44100 //16000/44100/48000 */
#define PLNK_DEMO_SCLK          2400000/*1M-4M,SCLK/SR需为整数且在1-4096范围*/
#define PLNK_DEMO_CH_NUM 	     1  /*支持的最大通道(max = 2)*/
#define PLNK_DEMO_IRQ_POINTS    256 /*采样中断点数*/
#define PLNK_DEMO_SCLK_IO       IO_PORTA_06
#define PLNK_DEMO_DAT0_IO       IO_PORTA_07
#define PLNK_DEMO_DAT1_IO       IO_PORTA_08

struct pdm_link_demo {
    PLNK_PARM plnk_mic;
    u8 mic_2_dac;
};

static struct pdm_link_demo *plnk_demo = NULL;
extern struct audio_dac_hdl dac_hdl;

/*
*********************************************************************
*                  AUDIO PDM OUTPUT
* Description: pdm mic数据回调
* Arguments  : data	mic数据
*			   len	数据长度 byte
* Return	 : None.
* Note(s)    : 单声道数据格式
*			   L0 L1 L2 L3 L4 ...
*				双声道数据格式()
*			   L0 R0 L1 R1 L2 R2...
*********************************************************************
*/
static void audio_plnk_mic_output(void *priv, void *data, u32 len)
{
    putchar('o');
    s16 *paddr = data;
    if (plnk_demo->mic_2_dac) {
        int wlen = audio_dac_write(&dac_hdl, paddr, len);
        if (wlen != len) {
            printf("pdm demo dac write err %d %d", wlen, len);
        }
    }
}

/*
*********************************************************************
*                  AUDIO PDM MIC OPEN
* Description: 打开pdm mic模块
* Arguments  : sample 		pdm mic采样率
*			   mic_2_dac 	mic数据（通过DAC）监听
* Return	 : None.
* Note(s)    : (1)sclk_fre:1M - 4M,sclk_fre / sr : 1 - 4096且须为整数
*              (2)打开pdm mic通道示例：
*				audio_plnk_demo_open(16000, 1);
*********************************************************************
*/
void audio_plnk_demo_open(u32 sample, u8 mic_2_dac)
{
    printf("pdm link demo\n");

    plnk_demo = zalloc(sizeof(struct pdm_link_demo));
    if (plnk_demo) {
        plnk_demo->plnk_mic.sr = sample;
        plnk_demo->plnk_mic.dma_len = (PLNK_DEMO_IRQ_POINTS << 1);
        plnk_demo->plnk_mic.sclk_io = PLNK_DEMO_SCLK_IO;
#if (PLNK_DEMO_CH_NUM > 0)
        plnk_demo->plnk_mic.ch_cfg[0].en = 1;
        plnk_demo->plnk_mic.ch_cfg[0].mode = DATA0_SCLK_RISING_EDGE;
        plnk_demo->plnk_mic.ch_cfg[0].mic_type = DIGITAL_MIC_DATA;

        plnk_demo->plnk_mic.data_cfg[0].en = 1;
        plnk_demo->plnk_mic.data_cfg[0].io = PLNK_DEMO_DAT0_IO;
#endif
#if (PLNK_DEMO_CH_NUM > 1)
        plnk_demo->plnk_mic.ch_cfg[1].en = 1;
        plnk_demo->plnk_mic.ch_cfg[1].mode = DATA0_SCLK_FALLING_EDGE;
        plnk_demo->plnk_mic.ch_cfg[1].mic_type = DIGITAL_MIC_DATA;

        plnk_demo->plnk_mic.data_cfg[0].en = 1;
        plnk_demo->plnk_mic.data_cfg[0].io = PLNK_DEMO_DAT1_IO;
#endif
        plnk_demo->plnk_mic.isr_cb = audio_plnk_mic_output;

        plnk_demo->mic_2_dac = mic_2_dac;
        if (plnk_demo->mic_2_dac) {
            app_audio_state_switch(APP_AUDIO_STATE_MUSIC, app_audio_volume_max_query(AppVol_BT_MUSIC), NULL);
            audio_dac_set_volume(&dac_hdl, 15);
            audio_dac_set_sample_rate(&dac_hdl, sample);
            audio_dac_start(&dac_hdl);
            audio_dac_channel_start(NULL);
            /*需要预填一些数据，处理中断写数dac播空打印U*/
            s16 *zero_data = zalloc(60);
            for (int i = 0; i < 100; i++) {
                audio_dac_write(&dac_hdl, zero_data, 60);
            }
            free(zero_data);
        }

        plnk_init(&plnk_demo->plnk_mic);
        plnk_start(&plnk_demo->plnk_mic);
    }
    while (1) {
        os_time_dly(10);
    };
}

/*
*********************************************************************
*                  AUDIO PDM MIC CLOSE
* Description: 关闭pdm mic模块
* Arguments  : None.
* Return	 : None.
* Note(s)    : None.
*********************************************************************
*/
void audio_plnk_demo_close(void)
{
    if (plnk_demo) {
        plnk_uninit(&plnk_demo->plnk_mic);
        if (plnk_demo->mic_2_dac) {
            audio_dac_stop(&dac_hdl);
            audio_dac_channel_close(NULL);
        }
        free(plnk_demo);
        plnk_demo = NULL;
    }
}

#if AUDIO_DEMO_LP_REG_ENABLE
static u8 plnk_demo_idle_query()
{
    return plnk_demo ? 0 : 1;
}

REGISTER_LP_TARGET(plnk_demo_lp_target) = {
    .name = "plnk_demo",
    .is_idle = plnk_demo_idle_query,
};
#endif/*AUDIO_DEMO_LP_REG_ENABLE*/
