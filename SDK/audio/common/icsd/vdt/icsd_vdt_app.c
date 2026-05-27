
#include "app_config.h"
#if ((defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN && \
	 TCFG_AUDIO_ANC_ENABLE && TCFG_AUDIO_SPEAK_TO_CHAT_ENABLE)
#include "system/includes.h"
#include "icsd_adt_app.h"
#include "audio_anc.h"
#include "asm/anc.h"
#include "tone_player.h"
#include "audio_config.h"
#include "icsd_anc_user.h"
#include "sniff.h"
#include "btstack/avctp_user.h"
#include "bt_tws.h"

#if TCFG_AUDIO_ANC_REAL_TIME_ADAPTIVE_ENABLE
#include "rt_anc_app.h"
#endif

/************************* start 智能免摘相关接口 ***********************/

/*智能免摘识别结果输出回调*/
void audio_speak_to_chat_output_handle(u8 voice_state)
{
    /* printf("%s, voice_state:%d", __func__, voice_state); */
    u8 data[4] = {SYNC_ICSD_ADT_VOICE_STATE, voice_state, 0, 0};

    if (voice_state) {
#if TCFG_USER_TWS_ENABLE
        if (tws_in_sniff_state() && (tws_api_get_role() == TWS_ROLE_MASTER)) {
            /*如果在蓝牙siniff下需要退出蓝牙sniff再发送*/
            icsd_adt_tx_unsniff_req();
        }
#endif
        /*同步状态*/
        audio_icsd_adt_info_sync(data, 4);
    }
}

/*打开智能免摘*/
int audio_speak_to_chat_open()
{
    if (anc_mode_get() == ANC_ON) {
#if !(SPEAK_TO_CHAT_ANC_MODE_ENABLE & ANC_ON_BIT)
        /*不支持anc on下打开免摘*/
        return 0;
#endif
    } else if (anc_mode_get() == ANC_OFF) {
#if !(SPEAK_TO_CHAT_ANC_MODE_ENABLE & ANC_OFF_BIT)
        /*不支持anc off下打开免摘*/
        return 0;
#endif
    } else if (anc_mode_get() == ANC_TRANSPARENCY) {
        /*不支持通透下开免摘*/
#if !(SPEAK_TO_CHAT_ANC_MODE_ENABLE & ANC_TRANS_BIT)
        return 0;
#endif
    }
    printf("%s: %d", __func__, __LINE__);
    u8 adt_mode = ADT_SPEAK_TO_CHAT_MODE;
    return audio_icsd_adt_sync_open(adt_mode);
}

/*关闭智能免摘*/
int audio_speak_to_chat_close()
{
    printf("%s", __func__);
    u8 adt_mode = ADT_SPEAK_TO_CHAT_MODE;
    return audio_icsd_adt_sync_close(adt_mode, 0);
}

void audio_speak_to_chat_demo()
{
    printf("%s", __func__);
    if (audio_icsd_adt_open_permit(ADT_SPEAK_TO_CHAT_MODE) == 0) {
        return;
    }

    /*判断智能免摘是否已经打开*/
    if ((get_icsd_adt_mode() & ADT_SPEAK_TO_CHAT_MODE) == 0) {
        if (anc_mode_get() == ANC_ON) {
#if !(SPEAK_TO_CHAT_ANC_MODE_ENABLE & ANC_ON_BIT)
            /*不支持anc on下打开免摘*/
            return;
#endif
        } else if (anc_mode_get() == ANC_OFF) {
#if !(SPEAK_TO_CHAT_ANC_MODE_ENABLE & ANC_OFF_BIT)
            /*不支持anc off下打开免摘*/
            return;
#endif
        } else if (anc_mode_get() == ANC_TRANSPARENCY) {
            /*暂不支持通透下开免摘*/
#if !(SPEAK_TO_CHAT_ANC_MODE_ENABLE & ANC_TRANS_BIT)
            return;
#endif
        }
        /*打开提示音*/
        icsd_adt_tone_play(ICSD_ADT_TONE_SPKCHAT_ON);
        audio_speak_to_chat_open();
    } else {
        /*关闭提示音*/
        icsd_adt_tone_play(ICSD_ADT_TONE_SPKCHAT_OFF);
        audio_speak_to_chat_close();
    }
}

#endif /*(defined TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN) && TCFG_AUDIO_ANC_ACOUSTIC_DETECTOR_EN*/
