#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".lp_touch_key_eartch.data.bss")
#pragma data_seg(".lp_touch_key_eartch.data")
#pragma const_seg(".lp_touch_key_eartch.text.const")
#pragma code_seg(".lp_touch_key_eartch.text")
#endif


#if TCFG_LP_EARTCH_KEY_ENABLE


#include "eartouch_manage.h"

#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
#include "lp_touch_key_eartch_algo.h"
#endif

#if TCFG_LP_EARTCH_DETECT_RELY_AUDIO
#include "icsd_adt_app.h"
#endif


#define EARTCH_DET_IN_EAR_BY_TOUCH_TRIGGER_EITHER		    0//触发其中一个通道则可认为入耳，单点和双点触摸均可兼容
#define EARTCH_DET_IN_EAR_BY_TOUCH_TRIGGER_TOGETHER		    1//同时触发两个通过才可认为入耳，仅用于双点触摸
#define EARTCH_DET_IN_EAR_TOUCH_LOGIC                       EARTCH_DET_IN_EAR_BY_TOUCH_TRIGGER_EITHER


u32 lp_touch_key_eartch_get_state(void)
{
    log_debug("get eartch_detect_result = %d\n", __this->eartch.ear_state);
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    return __this->eartch.ear_state;
#else
    return M2P_CTMU_OUTEAR_VALUE_H;
#endif
}

void lp_touch_key_eartch_set_state(u32 state)
{
    __this->eartch.ear_state = state;

#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
#else
    M2P_CTMU_OUTEAR_VALUE_H = state;
#endif
    log_debug("set eartch_detect_result = %d\n", __this->eartch.ear_state);
}

u32 lp_touch_key_eartch_state_is_out_ear(void)
{
    if (lp_touch_key_eartch_get_state()) {
        return 0;
    }
    return 1;
}

u32 lp_touch_key_eartch_get_ch_l_up_th(void)
{
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    u32 ch_idx = lp_touch_key_get_idx_by_cur_ch(__this->eartch.ch_list[0]);
    u32 up_th = lp_touch_key_eartch_algorithm_get_inear_up_th(ch_idx);
#else
    u32 up_th = P2M_CTMU_EARTCH_L_DIFF_VALUE;
#endif
    log_debug("get eartch_touch_detect_ch_l_up_th = %d\n", up_th);
    return up_th;
}

void lp_touch_key_eartch_set_ch_l_up_th(u32 up_th)
{
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    u32 ch_idx = lp_touch_key_get_idx_by_cur_ch(__this->eartch.ch_list[0]);
    lp_touch_key_eartch_algorithm_set_inear_up_th(ch_idx, up_th);
#else
    M2P_CTMU_EARTCH_TRIM_VALUE_L = up_th;
#endif
    log_debug("set eartch_touch_detect_ch_l_up_th = %d\n", up_th);
}

u32 lp_touch_key_eartch_get_ch_h_up_th(void)
{
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    u32 ch_idx = lp_touch_key_get_idx_by_cur_ch(__this->eartch.ch_list[1]);
    u32 up_th = lp_touch_key_eartch_algorithm_get_inear_up_th(ch_idx);
#else
    u32 up_th = P2M_CTMU_EARTCH_H_DIFF_VALUE;
#endif
    log_debug("get eartch_touch_detect_ch_h_up_th = %d\n", up_th);
    return up_th;
}

void lp_touch_key_eartch_set_ch_h_up_th(u32 up_th)
{
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    u32 ch_idx = lp_touch_key_get_idx_by_cur_ch(__this->eartch.ch_list[1]);
    lp_touch_key_eartch_algorithm_set_inear_up_th(ch_idx, up_th);
#else
    M2P_CTMU_EARTCH_TRIM_VALUE_H = up_th;
#endif
    log_debug("set eartch_touch_detect_ch_h_up_th = %d\n", up_th);
}

u32 lp_touch_key_eartch_get_each_ch_state(void)
{
    u32 state = 0;
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    u32 ch, ch_idx;
    for (u32 i = 0; i < __this->eartch.ch_num; i ++) {
        ch = __this->eartch.ch_list[i];
        ch_idx = lp_touch_key_get_idx_by_cur_ch(ch);
        if (lp_touch_key_eartch_algorithm_get_ear_state(ch_idx)) {
            state |= BIT(ch);
        }
    }
#else
    state = P2M_CTMU_EARTCH_EVENT;

#endif
    log_debug("get eartch_touch_each_ch_state = 0x%x\n", state);
    return state;
}

void lp_touch_key_eartch_set_each_ch_state(u32 state)
{
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    u32 ch, ch_idx;
    for (u32 i = 0; i < __this->eartch.ch_num; i ++) {
        ch = __this->eartch.ch_list[i];
        ch_idx = lp_touch_key_get_idx_by_cur_ch(ch);
        if (state & BIT(ch)) {
            lp_touch_key_eartch_algorithm_set_ear_state(ch_idx, 1);
        }
    }
#else
    M2P_CTMU_INEAR_VALUE_H = state;
#endif
    log_debug("set eartch_touch_each_ch_state = 0x%x\n", state);
}

u32 lp_touch_key_eartch_touch_trigger_together(void)
{
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    u32 ch, ch_en = 0;
    for (u32 i = 0; i < __this->eartch.ch_num; i ++) {
        ch = __this->eartch.ch_list[i];
        ch_en |= BIT(ch);
    }
#else
    u32 ch_en = M2P_CTMU_EARTCH_CH;
#endif

    if (lp_touch_key_eartch_get_each_ch_state() == ch_en) {
        return 1;
    }
    return 0;
}

u32 lp_touch_key_eartch_touch_trigger_either(void)
{
    if (lp_touch_key_eartch_get_each_ch_state()) {
        return 1;
    }
    return 0;
}

u32 lp_touch_key_eartch_touch_is_invalid(void)
{
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    u32 invalid = 0;
    u32 ch, ch_idx;
    for (u32 i = 0; i < __this->eartch.ch_num; i ++) {
        ch = __this->eartch.ch_list[i];
        ch_idx = lp_touch_key_get_idx_by_cur_ch(ch);
        if (lp_touch_key_eartch_algorithm_is_invalid(ch_idx)) {
            invalid |= BIT(ch);
        }
    }
#else
    u32 invalid = P2M_CTMU_EARTCH_L_IIR_VALUE;
#endif
    log_debug("eartch_touch_algo_invalid = 0x%x\n", invalid);
    return invalid;
}

void lp_touch_key_eartch_set_touch_valid_time(u32 time)
{
#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
    u32 ch, ch_idx;
    for (u32 i = 0; i < __this->eartch.ch_num; i ++) {
        ch = __this->eartch.ch_list[i];
        ch_idx = lp_touch_key_get_idx_by_cur_ch(ch);
        lp_touch_key_eartch_algorithm_set_win_data_len(ch_idx, time);
    }
#else
    M2P_CTMU_OUTEAR_VALUE_L = time; //(x + 2) * 8 * 20;
#endif
}


/***************************************************************************************/
/***************************************************************************************/

void __attribute__((weak)) eartch_notify_event(u32 state)
{
    eartouch_event_handler(state);
}

static void lp_touch_key_eartch_notify_event(u32 state)
{
    if (get_charge_online_flag()) {
        state = EARTCH_MUST_BE_OUT_EAR;
    }

    u32 user_state;

    switch (state) {
    case EARTCH_MUST_BE_OUT_EAR:
        lp_touch_key_eartch_set_state(0);
        user_state = EARTOUCH_STATE_OUT_EAR;
        break;
    case EARTCH_MUST_BE_IN_EAR:
        lp_touch_key_eartch_set_state(1);
        user_state = EARTOUCH_STATE_IN_EAR;
        break;
    case EARTCH_MAY_BE_OUT_EAR:
        user_state = EARTOUCH_STATE_OUT_EAR;
        break;
    case EARTCH_MAY_BE_IN_EAR:
        user_state = EARTOUCH_STATE_IN_EAR;
        break;
    default :
        user_state = EARTOUCH_STATE_OUT_EAR;
        break;
    }

    if (__this->eartch.user_ear_state != user_state) {
        __this->eartch.user_ear_state = user_state;
        eartch_notify_event(user_state);
    }
}

u32 lp_touch_key_eartch_get_user_ear_state(void)
{
    return __this->eartch.user_ear_state;
}

#if TCFG_LP_EARTCH_DETECT_RELY_AUDIO

void lp_touch_key_eartch_trigger_audio_detect(void)
{
    u32 state = lp_touch_key_eartch_get_state();
    log_debug("audio_ear_in_detect_open ---> %d\n", state);
    audio_ear_in_detect_open(state);
}

void lp_touch_key_eartch_close_audio_detect(void)
{
    log_debug("audio_ear_in_detect_close\n");
    audio_ear_in_detect_close();
}

void lp_touch_key_eartch_audio_detect_valid(void *priv)
{
    __this->eartch.audio_det_invalid_timeout = 0;
}

void lp_touch_key_eartch_audio_detect_valid_deal(void)
{
    u32 state = 0;
    u32 in_ear_valid = BIT((__this->pdata->eartch_audio_det_filter_param + 1)) - 1;//BIT(6+1)-1=0b111111;
    u32 det_valid = __this->eartch.audio_det_valid & in_ear_valid;
    log_debug("eartch_audio_detect_result: %x\n", det_valid);

    if (det_valid == 0) {
        state = EARTCH_MUST_BE_OUT_EAR;
    } else if (det_valid == in_ear_valid) {
        state = EARTCH_MUST_BE_IN_EAR;
    } else {
        return;
    }
    if ((__this->eartch.touch_invalid == 0) && (__this->eartch.audio_det_invalid_timeout == 0)) {
        lp_touch_key_eartch_close_audio_detect();
        lp_touch_key_eartch_notify_event(state);
    }
}

void lp_touch_key_eartch_audio_detect_cbfunc(u32 det_result)
{
    __this->eartch.audio_det_valid <<= 1;
    __this->eartch.audio_det_valid += det_result;
    lp_touch_key_eartch_audio_detect_valid_deal();
}

#else


void lp_touch_key_eartch_touch_valid_deal(void)
{
#if (EARTCH_DET_IN_EAR_TOUCH_LOGIC == EARTCH_DET_IN_EAR_BY_TOUCH_TRIGGER_TOGETHER)
    if (lp_touch_key_eartch_state_is_out_ear()) {
        if (lp_touch_key_eartch_touch_trigger_together()) {
            lp_touch_key_eartch_notify_event(EARTCH_MUST_BE_IN_EAR);
        } else {
            lp_touch_key_eartch_notify_event(EARTCH_MUST_BE_OUT_EAR);
        }
    } else
#endif
    {
        if (lp_touch_key_eartch_touch_trigger_either()) {
            lp_touch_key_eartch_notify_event(EARTCH_MUST_BE_IN_EAR);
        } else {
            lp_touch_key_eartch_notify_event(EARTCH_MUST_BE_OUT_EAR);
        }
    }
}

#endif


void lp_touch_key_eartch_check_touch_valid(void *priv)
{
    if (lp_touch_key_eartch_touch_is_invalid()) {
        return;
    }

    lp_touch_key_eartch_touch_valid_deal();

    if (__this->eartch.check_touch_valid_timer) {
        sys_hi_timer_del(__this->eartch.check_touch_valid_timer);
        __this->eartch.check_touch_valid_timer = 0;
    }
}

void lp_touch_key_eartch_touch_invalid_timeout(void *priv)
{
    __this->eartch.touch_invalid_timeout = 0;
    log_debug("eartch_touch_invalid_timeout");
#if TCFG_LP_EARTCH_DETECT_RELY_AUDIO
    lp_touch_key_eartch_notify_event(EARTCH_MAY_BE_IN_EAR);
#else
    lp_touch_key_eartch_notify_event(EARTCH_MUST_BE_IN_EAR);
#endif
}

static void lp_touch_key_eartch_event_deal(u32 event)
{
    if (event) {

#if (EARTCH_DET_IN_EAR_TOUCH_LOGIC == EARTCH_DET_IN_EAR_BY_TOUCH_TRIGGER_EITHER)
        if (__this->eartch.touch_invalid == 0) {
#else
        if (__this->eartch.touch_invalid == 1) {
#endif

            if (lp_touch_key_eartch_state_is_out_ear()) {
                __this->eartch.touch_invalid_timeout = sys_hi_timeout_add(NULL, lp_touch_key_eartch_touch_invalid_timeout, __this->pdata->eartch_touch_filter_time);
            }

#if TCFG_LP_EARTCH_DETECT_RELY_AUDIO
            lp_touch_key_eartch_trigger_audio_detect();
#endif
        }

#if TCFG_LP_EARTCH_DETECT_RELY_AUDIO
        if (__this->audio_det_invalid_timeout) {
            sys_hi_timeout_del(__this->eartch.audio_det_invalid_timeout);
            __this->eartch.audio_det_invalid_timeout = 0;
        }
#else
        if (__this->eartch.check_touch_valid_timer) {
            sys_hi_timer_del(__this->eartch.check_touch_valid_timer);
            __this->eartch.check_touch_valid_timer = 0;
        }
#endif

        __this->eartch.touch_invalid ++;

    } else {
        if (__this->eartch.touch_invalid) {
            __this->eartch.touch_invalid --;
        }

#if (EARTCH_DET_IN_EAR_TOUCH_LOGIC == EARTCH_DET_IN_EAR_BY_TOUCH_TRIGGER_TOGETHER)

        if (__this->eartch.touch_invalid_timeout) {
            sys_hi_timeout_del(__this->eartch.touch_invalid_timeout);
            __this->eartch.touch_invalid_timeout = 0;
            log_debug("sys_hi_timeout_del  eartch_touch_invalid_timeout");
        }
#endif

        if (__this->eartch.touch_invalid == 0) {

#if (EARTCH_DET_IN_EAR_TOUCH_LOGIC == EARTCH_DET_IN_EAR_BY_TOUCH_TRIGGER_EITHER)

            if (__this->eartch.touch_invalid_timeout) {
                sys_hi_timeout_del(__this->eartch.touch_invalid_timeout);
                __this->eartch.touch_invalid_timeout = 0;
                log_debug("sys_hi_timeout_del  eartch_touch_invalid_timeout");
            }
#endif

#if TCFG_LP_EARTCH_DETECT_RELY_AUDIO

            if (__this->eartch.audio_det_invalid_timeout == 0) {
                __this->eartch.audio_det_invalid_timeout = sys_hi_timeout_add(NULL, lp_touch_key_eartch_audio_detect_valid, __this->pdata->eartch_audio_det_valid_time);
            } else {
                sys_hi_timeout_modify(__this->eartch.audio_det_invalid_timeout, __this->pdata->eartch_audio_det_valid_time);
            }
#else

            if (lp_touch_key_eartch_touch_is_invalid()) {
                if (__this->eartch.check_touch_valid_timer == 0) {
                    __this->eartch.check_touch_valid_timer = sys_hi_timer_add(NULL, lp_touch_key_eartch_check_touch_valid, __this->pdata->eartch_check_touch_valid_time);
                } else {
                    sys_hi_timer_modify(__this->eartch.check_touch_valid_timer, __this->pdata->eartch_check_touch_valid_time);
                }
            } else {
                if (__this->eartch.check_touch_valid_timer) {
                    sys_hi_timer_del(__this->eartch.check_touch_valid_timer);
                    __this->eartch.check_touch_valid_timer = 0;
                }
                lp_touch_key_eartch_touch_valid_deal();
            }
#endif

        }
    }

    log_debug("eartch_touch_invalid = %d\n", __this->eartch.touch_invalid);

}

void lp_touch_key_eartch_init(void)
{
    if (__this->eartch.ch_num == 0) {
        return;
    }
    u32 valid_time = 16 * __this->pdata->lpctmu_pdata->sample_scan_time;
    if (__this->pdata->eartch_touch_valid_time > valid_time) {
        valid_time = __this->pdata->eartch_touch_valid_time - valid_time;
        valid_time /= __this->pdata->lpctmu_pdata->sample_scan_time;
        valid_time /= 8;
    } else {
        valid_time = 6;
    }
    lp_touch_key_eartch_set_touch_valid_time(valid_time);

    lp_touch_key_eartch_set_state(0);

    __this->eartch.user_ear_state = 0xff;
}

void lp_touch_key_eartch_state_reset(void)
{
    if (__this->eartch.ch_num == 0) {
        return;
    }
    struct eartch_inear_info inear_info;
    int ret = syscfg_read(LP_KEY_EARTCH_TRIM_VALUE, (void *)&inear_info, sizeof(struct eartch_inear_info));
    if (ret != sizeof(struct eartch_inear_info)) {
        return;
    }
    if (inear_info.valid == 0) {
        return;
    }
    inear_info.valid = 0;
    lp_touch_key_eartch_set_state(1);
    lp_touch_key_eartch_set_each_ch_state(inear_info.p2m_each_ch_state);
    lp_touch_key_eartch_set_ch_l_up_th(inear_info.p2m_ch_l_up_th);
    __this->lpctmu_cfg.ch_fixed_isel[__this->eartch.ch_list[0]] = inear_info.ctmu_ch_l_isel_level;
    if (__this->eartch.ch_num == 2) {
        lp_touch_key_eartch_set_ch_h_up_th(inear_info.p2m_ch_h_up_th);
        __this->lpctmu_cfg.ch_fixed_isel[__this->eartch.ch_list[1]] = inear_info.ctmu_ch_l_isel_level;
    }

#if TOUCH_KEY_EARTCH_ALGO_IN_MSYS
#else
    lpctmu_send_m2p_cmd(RESET_EARTCH_STATE);
#endif

    syscfg_write(LP_KEY_EARTCH_TRIM_VALUE, (void *)&inear_info, sizeof(struct eartch_inear_info));
}

void lp_touch_key_eartch_save_inear_info(void)
{
    if (__this->eartch.ch_num == 0) {
        return;
    }
    if (lp_touch_key_eartch_state_is_out_ear()) {
        return;
    }
    struct eartch_inear_info inear_info;
    inear_info.valid = 1;
    inear_info.p2m_each_ch_state =  lp_touch_key_eartch_get_each_ch_state();
    inear_info.p2m_ch_l_up_th = lp_touch_key_eartch_get_ch_l_up_th();
    inear_info.ctmu_ch_l_isel_level = lpctmu_get_ana_cur_level(__this->eartch.ch_list[0]);
    if (__this->eartch.ch_num == 2) {
        inear_info.p2m_ch_h_up_th = lp_touch_key_eartch_get_ch_h_up_th();
        inear_info.ctmu_ch_h_isel_level = lpctmu_get_ana_cur_level(__this->eartch.ch_list[1]);
    }
    syscfg_write(LP_KEY_EARTCH_TRIM_VALUE, (void *)&inear_info, sizeof(struct eartch_inear_info));
}

void lp_touch_key_testbox_inear_trim(u32 flag)
{
}

#endif

