#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".lp_touch_key.data.bss")
#pragma data_seg(".lp_touch_key.data")
#pragma const_seg(".lp_touch_key.text.const")
#pragma code_seg(".lp_touch_key.text")
#endif
#include "system/includes.h"
#include "app_config.h"
#include "app_msg.h"
#include "key_driver.h"
#include "key_event_deal.h"
#include "lp_touch_key_range_algo.h"
#include "asm/lp_touch_key_tool.h"
#include "asm/lp_touch_key_api.h"
#include "asm/power_interface.h"
#include "asm/charge.h"
#include "app_charge.h"
#include "btstack/avctp_user.h"




#define LOG_TAG_CONST       LP_KEY
#define LOG_TAG             "[LP_KEY]"
/* #define LOG_ERROR_ENABLE */
/* #define LOG_DEBUG_ENABLE */
#define LOG_INFO_ENABLE
#define LOG_DUMP_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"



#if TCFG_LP_TOUCH_KEY_ENABLE


VM_MANAGE_ID_REG(touch_key0_algo_cfg, VM_LP_TOUCH_KEY0_ALOG_CFG);
VM_MANAGE_ID_REG(touch_key1_algo_cfg, VM_LP_TOUCH_KEY1_ALOG_CFG);
VM_MANAGE_ID_REG(touch_key2_algo_cfg, VM_LP_TOUCH_KEY2_ALOG_CFG);


static volatile u8 is_lpkey_active = 0;
static volatile u8 touch_irq_before_init = 0;
static struct lp_touch_key_config_data lp_touch_key_cfg_data;
#define __this (&lp_touch_key_cfg_data)




static void lp_touch_key_reset_algo(void)
{
    lpctmu_send_m2p_cmd(RESET_IDENTIFY_ALGO);
}

static void lp_touch_key_identify_algorithm_init(void)
{
    lp_touch_key_reset_algo();
}

static u32 lp_touch_key_identify_algo_get_edge_down_th(ch_idx)
{
    const struct touch_key_cfg *key_cfg = &(__this->pdata->key_cfg[ch_idx]);
    u32 ch = key_cfg->key_ch;
    u32 cfg2 = (M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2H + ch * 8) << 8) | M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2L + ch * 8);
    return cfg2;
}

static void lp_touch_key_identify_algo_set_edge_down_th(u32 ch_idx, u32 cfg2_new)
{
    u32 ch = __this->pdata->key_cfg[ch_idx].key_ch;

    u16 cfg0 = (M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG0H + ch * 8) << 8) | M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG0L + ch * 8);
    u16 cfg1 = (M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG1H + ch * 8) << 8) | M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG1L + ch * 8);
    u16 cfg2 = (M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2H + ch * 8) << 8) | M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2L + ch * 8);
    if (cfg2_new < (3 * cfg0)) {
        cfg2_new = (3 * cfg0);
    }
    if (cfg2 != cfg2_new) {
        log_debug("ctmu ch%d cfg2_old = %d  cfg2_new = %d\n", ch, cfg2, cfg2_new);
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2L + ch * 8) = (cfg2_new >> 0) & 0xff;
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2H + ch * 8) = (cfg2_new >> 8) & 0xff;
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG0L + ch * 8) = (cfg1 >> 0) & 0xff;
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG0H + ch * 8) = (cfg1 >> 8) & 0xff;
    }
}



#include "../../../components/touch/lp_touch_key_common.c"

#include "../../../components/touch/lp_touch_key_eartch.c"

#include "../../../components/touch/lp_touch_key_click.c"

#include "../../../components/touch/lp_touch_key_slide.c"



static u32 lp_touch_key_get_lpctmu_res_value(u32 ch_idx)
{
    u16 ctmu_res0;
    u16 ctmu_res1;
    u32 ch = __this->pdata->key_cfg[ch_idx].key_ch;
__read_res:
    ctmu_res0 = (P2M_MESSAGE_ACCESS(P2M_MASSAGE_CTMU_CH0_H_RES + ch * 2) << 8) | P2M_MESSAGE_ACCESS(P2M_MASSAGE_CTMU_CH0_L_RES + ch * 2);
    delay_nops(100);
    ctmu_res1 = (P2M_MESSAGE_ACCESS(P2M_MASSAGE_CTMU_CH0_H_RES + ch * 2) << 8) | P2M_MESSAGE_ACCESS(P2M_MASSAGE_CTMU_CH0_L_RES + ch * 2);
    if (ctmu_res0 != ctmu_res1) {
        goto __read_res;
    }
    return ctmu_res0;
}


static void lp_touch_key_range_algo_analyze_scan(void *priv)
{
    u32 ch_res, ch_idx;
    for (ch_idx = 0; ch_idx < __this->pdata->key_num; ch_idx ++) {
        ch_res = lp_touch_key_get_lpctmu_res_value(ch_idx);
        lp_touch_key_range_algo_analyze(ch_idx, ch_res);
    }
}

void lp_touch_key_init(const struct lp_touch_key_platform_data *pdata)
{
    log_info("%s >>>>", __func__);

    u32 ch_idx, ch;
    const struct touch_key_cfg *key_cfg;
    const struct touch_key_algo_cfg *algo_cfg;

    memset(__this, 0, sizeof(struct lp_touch_key_config_data));
    __this->pdata = pdata;
    if (!__this->pdata) {
        return;
    }

    __this->lpctmu_cfg.pdata = __this->pdata->lpctmu_pdata;
    for (ch_idx = 0; ch_idx < __this->pdata->key_num; ch_idx ++) {
        key_cfg = &(__this->pdata->key_cfg[ch_idx]);
        ch = key_cfg->key_ch;
        __this->lpctmu_cfg.ch_num = __this->pdata->key_num;
        __this->lpctmu_cfg.ch_list[ch_idx] = ch;
        __this->lpctmu_cfg.ch_en |= BIT(ch);
        __this->lpctmu_cfg.ch_fixed_isel[ch] = 0;
        if (key_cfg->wakeup_enable) {
            __this->lpctmu_cfg.ch_wkp_en |= BIT(ch);
        }
    }
    if (__this->lpctmu_cfg.ch_wkp_en) {
        __this->lpctmu_cfg.softoff_wakeup_cfg = LPCTMU_WAKEUP_EN_WITHOUT_CHARGE_ONLINE;
        if (__this->pdata->charge_online_softoff_wakeup) {
            __this->lpctmu_cfg.softoff_wakeup_cfg = LPCTMU_WAKEUP_EN_ALWAYS;
        }
    } else {
        __this->lpctmu_cfg.softoff_wakeup_cfg = LPCTMU_WAKEUP_DISABLE;
    }

    M2P_CTMU_CH_ENABLE = __this->lpctmu_cfg.ch_en;
    M2P_CTMU_CH_WAKEUP_EN = __this->lpctmu_cfg.ch_wkp_en;
    M2P_CTMU_SCAN_TIME = __this->lpctmu_cfg.pdata->sample_scan_time;
    M2P_CTMU_LOWPOER_SCAN_TIME = __this->lpctmu_cfg.pdata->lowpower_sample_scan_time;

    M2P_CTMU_CMD = 0;
    M2P_CTMU_CH_DEBUG = 0;
    M2P_CTMU_CH_CFG = 0;
    M2P_CTMU_EARTCH_CH = 0;

    M2P_CTMU_LONG_KEY_EVENT_TIMEL   = (__this->pdata->long_click_check_time >> 0) & 0xff;
    M2P_CTMU_LONG_KEY_EVENT_TIMEH   = (__this->pdata->long_click_check_time >> 8) & 0xff;
    M2P_CTMU_HOLD_KEY_EVENT_TIMEL   = (__this->pdata->hold_click_check_time >> 0) & 0xff;
    M2P_CTMU_HOLD_KEY_EVENT_TIMEH   = (__this->pdata->hold_click_check_time >> 8) & 0xff;
    M2P_CTMU_SOFTOFF_WAKEUP_TIMEL   = (__this->pdata->softoff_wakeup_time   >> 0) & 0xff;
    M2P_CTMU_SOFTOFF_WAKEUP_TIMEH   = (__this->pdata->softoff_wakeup_time   >> 8) & 0xff;

#if TCFG_LP_TOUCH_KEY_BT_TOOL_ENABLE
    M2P_CTMU_LONG_PRESS_RESET_TIMEL = 0;
    M2P_CTMU_LONG_PRESS_RESET_TIMEH = 0;
#else
    M2P_CTMU_LONG_PRESS_RESET_TIMEL = (__this->pdata->long_press_reset_time >> 0) & 0xff;
    M2P_CTMU_LONG_PRESS_RESET_TIMEH = (__this->pdata->long_press_reset_time >> 8) & 0xff;
#endif

    for (ch_idx = 0; ch_idx < __this->pdata->key_num; ch_idx ++) {
        key_cfg = &(__this->pdata->key_cfg[ch_idx]);
        algo_cfg = &(key_cfg->algo_cfg[key_cfg->index]);
        ch = key_cfg->key_ch;
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG0L + ch * 8) = (algo_cfg->algo_cfg0 >> 0) & 0xff;
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG0H + ch * 8) = (algo_cfg->algo_cfg0 >> 8) & 0xff;
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG1L + ch * 8) = ((algo_cfg->algo_cfg0 +  5) >> 0) & 0xff;
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG1H + ch * 8) = ((algo_cfg->algo_cfg0 +  5) >> 8) & 0xff;
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2L + ch * 8) = (algo_cfg->algo_cfg2 >> 0) & 0xff;
        M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2H + ch * 8) = (algo_cfg->algo_cfg2 >> 8) & 0xff;
        log_debug("M2P_CTMU_CH%d_CFG0L = 0x%x", ch, M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG0L + ch * 8));
        log_debug("M2P_CTMU_CH%d_CFG0H = 0x%x", ch, M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG0H + ch * 8));
        log_debug("M2P_CTMU_CH%d_CFG1L = 0x%x", ch, M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG1L + ch * 8));
        log_debug("M2P_CTMU_CH%d_CFG1H = 0x%x", ch, M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG1H + ch * 8));
        log_debug("M2P_CTMU_CH%d_CFG2L = 0x%x", ch, M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2L + ch * 8));
        log_debug("M2P_CTMU_CH%d_CFG2H = 0x%x", ch, M2P_MESSAGE_ACCESS(M2P_MASSAGE_CTMU_CH0_CFG2H + ch * 8));

#if TCFG_LP_EARTCH_KEY_ENABLE
        if (key_cfg->eartch_en) {
            M2P_CTMU_EARTCH_CH |= BIT(ch);
            __this->eartch.ch_list[__this->eartch.ch_num] = ch;
            __this->eartch.ch_num ++;
        } else
#endif
        {

#if !TCFG_LP_TOUCH_KEY_BT_TOOL_ENABLE
            lp_touch_key_range_algo_init(ch_idx, algo_cfg);
#endif

        }
    }

#if TCFG_LP_EARTCH_KEY_ENABLE
    lp_touch_key_eartch_init();
#endif

    if ((!is_wakeup_source(PWR_WK_REASON_P11)) || \
        (__this->pdata->ldo_wkp_algo_reset && is_ldo5v_wakeup()) || \
        (__this->pdata->charge_online_algo_reset && get_charge_online_flag())) {

        lp_touch_key_identify_algorithm_init();
    }

#if TCFG_LP_EARTCH_KEY_ENABLE
    lp_touch_key_eartch_state_reset();
#endif

    lpctmu_init(&__this->lpctmu_cfg);

#if TCFG_LP_TOUCH_KEY_BT_TOOL_ENABLE

    lp_touch_key_debug_init(
        1,
        lp_touch_key_online_debug_init,
        lp_touch_key_online_debug_send,
        lp_touch_key_online_debug_key_event_handle);

    if ((touch_bt_tool_enable) && (touch_bt_online_debug_init)) {
        touch_bt_online_debug_init();
    }

#else

    usr_timer_add(NULL, lp_touch_key_range_algo_analyze_scan, __this->lpctmu_cfg.pdata->sample_scan_time, 0);

#if CTMU_CHECK_LONG_CLICK_BY_RES
    lp_touch_key_ctmu_res_all_buf_clear();
#endif

#endif

    if (touch_irq_before_init) {
        log_debug("lp touch key irq before init %d !", touch_irq_before_init);
        extern void lp_touch_key_event_irq_handler();
        lp_touch_key_event_irq_handler();
        touch_irq_before_init = 0;
    }

    log_debug("M2P_CTMU_CH_ENABLE              = 0x%x\n", M2P_CTMU_CH_ENABLE);
    log_debug("M2P_CTMU_CH_WAKEUP_EN           = 0x%x\n", M2P_CTMU_CH_WAKEUP_EN);
    log_debug("M2P_CTMU_CH_DEBUG               = 0x%x\n", M2P_CTMU_CH_DEBUG);
    log_debug("M2P_CTMU_CH_CFG                 = 0x%x\n", M2P_CTMU_CH_CFG);
    log_debug("M2P_CTMU_SCAN_TIME              = %d\n", M2P_CTMU_SCAN_TIME);
    log_debug("M2P_CTMU_LOWPOER_SCAN_TIME      = %d\n", M2P_CTMU_LOWPOER_SCAN_TIME);
    log_debug("M2P_CTMU_LONG_KEY_EVENT_TIMEL   = %d\n", M2P_CTMU_LONG_KEY_EVENT_TIMEL);
    log_debug("M2P_CTMU_LONG_KEY_EVENT_TIMEH   = %d\n", M2P_CTMU_LONG_KEY_EVENT_TIMEH);
    log_debug("M2P_CTMU_HOLD_KEY_EVENT_TIMEL   = %d\n", M2P_CTMU_HOLD_KEY_EVENT_TIMEL);
    log_debug("M2P_CTMU_HOLD_KEY_EVENT_TIMEH   = %d\n", M2P_CTMU_HOLD_KEY_EVENT_TIMEH);
    log_debug("M2P_CTMU_SOFTOFF_WAKEUP_TIMEL   = %d\n", M2P_CTMU_SOFTOFF_WAKEUP_TIMEL);
    log_debug("M2P_CTMU_SOFTOFF_WAKEUP_TIMEH   = %d\n", M2P_CTMU_SOFTOFF_WAKEUP_TIMEH);
    log_debug("M2P_CTMU_LONG_PRESS_RESET_TIMEL = %d\n", M2P_CTMU_LONG_PRESS_RESET_TIMEL);
    log_debug("M2P_CTMU_LONG_PRESS_RESET_TIMEH = %d\n", M2P_CTMU_LONG_PRESS_RESET_TIMEH);
}

void lp_touch_key_event_irq_handler()
{
    if (!__this->pdata) {
        touch_irq_before_init += 1;
        return;
    }

    u32 ctmu_event = P2M_CTMU_KEY_EVENT;
    u32 ch = P2M_CTMU_KEY_CNT;
    u32 ch_idx = lp_touch_key_get_idx_by_cur_ch(ch);
    u16 ch_res = 0;
    u16 chx_res[LPCTMU_CHANNEL_SIZE];
    struct touch_key_arg *arg = &(__this->arg[ch_idx]);

    /* printf("ctmu_event = 0x%x\n", ctmu_event); */

    switch (ctmu_event) {
    case CTMU_P2M_CH0_RES_EVENT:
    case CTMU_P2M_CH1_RES_EVENT:
    case CTMU_P2M_CH2_RES_EVENT:
    case CTMU_P2M_CH3_RES_EVENT:
    case CTMU_P2M_CH4_RES_EVENT:
        chx_res[0] = (P2M_CTMU_CH0_H_RES << 8) | P2M_CTMU_CH0_L_RES;
        chx_res[1] = (P2M_CTMU_CH1_H_RES << 8) | P2M_CTMU_CH1_L_RES;
        chx_res[2] = (P2M_CTMU_CH2_H_RES << 8) | P2M_CTMU_CH2_L_RES;
        chx_res[3] = (P2M_CTMU_CH3_H_RES << 8) | P2M_CTMU_CH3_L_RES;
        chx_res[4] = (P2M_CTMU_CH4_H_RES << 8) | P2M_CTMU_CH4_L_RES;
        //cppcheck-suppress unreadVariable
        ch_res = chx_res[ch];
        /* printf("ch%d_res: %d\n", ch, ch_res); */

#if TCFG_LP_TOUCH_KEY_BT_TOOL_ENABLE
        if ((touch_bt_tool_enable) && (touch_bt_online_debug_send)) {
            touch_bt_online_debug_send(ch, ch_res);
        }
#endif

        break;

    case CTMU_P2M_CH0_LONG_KEY_EVENT:
    case CTMU_P2M_CH1_LONG_KEY_EVENT:
    case CTMU_P2M_CH2_LONG_KEY_EVENT:
    case CTMU_P2M_CH3_LONG_KEY_EVENT:
    case CTMU_P2M_CH4_LONG_KEY_EVENT:
        ctmu_event = TOUCH_KEY_LONG_EVENT;
        log_debug("CH%d: LONG click", ch);
        is_lpkey_active &= ~BIT(ch_idx);

        if (__this->pdata->slide_mode_en) {
        } else {
#if CTMU_CHECK_LONG_CLICK_BY_RES
            if (lp_touch_key_check_long_click_by_ctmu_res(ch_idx)) {
                return;
            }
#endif
            lp_touch_key_long_click_handle(ch_idx);
        }
        break;
    case CTMU_P2M_CH0_HOLD_KEY_EVENT:
    case CTMU_P2M_CH1_HOLD_KEY_EVENT:
    case CTMU_P2M_CH2_HOLD_KEY_EVENT:
    case CTMU_P2M_CH3_HOLD_KEY_EVENT:
    case CTMU_P2M_CH4_HOLD_KEY_EVENT:
        ctmu_event = TOUCH_KEY_HOLD_EVENT;
        log_debug("CH%d: HOLD click", ch);
        is_lpkey_active &= ~BIT(ch_idx);

        if (__this->pdata->slide_mode_en) {
        } else {
#if CTMU_CHECK_LONG_CLICK_BY_RES
            if (lp_touch_key_check_long_click_by_ctmu_res(ch_idx)) {
                return;
            }
#endif
            lp_touch_key_hold_click_handle(ch_idx);
        }
        break;
    case CTMU_P2M_CH0_FALLING_EVENT:
    case CTMU_P2M_CH1_FALLING_EVENT:
    case CTMU_P2M_CH2_FALLING_EVENT:
    case CTMU_P2M_CH3_FALLING_EVENT:
    case CTMU_P2M_CH4_FALLING_EVENT:
        ctmu_event = TOUCH_KEY_FALLING_EVENT;
        log_debug("CH%d: FALLING", ch);
        is_lpkey_active |=  BIT(ch_idx);

#if TCFG_LP_EARTCH_KEY_ENABLE
        if (M2P_CTMU_EARTCH_CH & BIT(ch)) {
            lp_touch_key_eartch_event_deal(1);
            return;
        }
#endif

#if CTMU_CHECK_LONG_CLICK_BY_RES
        arg->falling_res_avg = lp_touch_key_ctmu_res_buf_avg(ch_idx);
        log_debug("falling_res_avg: %d", arg->falling_res_avg);
#endif

        if (__this->pdata->slide_mode_en) {
        } else {
            lp_touch_key_send_key_tone_msg();
        }
        break;
    case CTMU_P2M_CH0_RAISING_EVENT:
    case CTMU_P2M_CH1_RAISING_EVENT:
    case CTMU_P2M_CH2_RAISING_EVENT:
    case CTMU_P2M_CH3_RAISING_EVENT:
    case CTMU_P2M_CH4_RAISING_EVENT:
        ctmu_event = TOUCH_KEY_RAISING_EVENT;
        log_debug("CH%d: RAISING", ch);
        is_lpkey_active &= ~BIT(ch_idx);

#if TCFG_LP_EARTCH_KEY_ENABLE
        if (M2P_CTMU_EARTCH_CH & BIT(ch)) {
            lp_touch_key_eartch_event_deal(0);
            return;
        }
#endif

#if CTMU_CHECK_LONG_CLICK_BY_RES
        lp_touch_key_ctmu_res_buf_clear(ch_idx);
#endif
        if (__this->pdata->slide_mode_en) {
        } else {
            lp_touch_key_raise_click_handle(ch_idx);
        }
        break;
    default:
        break;
    }

    if (__this->pdata->slide_mode_en) {
        u32 key_type = lp_touch_key_check_slide_key_type(ctmu_event, ch_idx);
        if (key_type) {
            log_debug("CH%d: key_type = 0x%x\n", ch, key_type);
            lp_touch_key_send_slide_key_type_event(key_type);
        }
    }
}

u32 lp_touch_key_power_on_status()
{
    if (P2M_CTMU_KEY_STATE) {
        return 1;//key active
    }
    return 0;
}

void lp_touch_key_disable(void)
{
    log_debug("%s", __func__);
    if (!__this->pdata) {
        return;
    }
    is_lpkey_active = 0;
    lpctmu_disable();
}

void lp_touch_key_enable(void)
{
    log_debug("%s", __func__);
    if (!__this->pdata) {
        return;
    }
    is_lpkey_active = 0;
    lpctmu_enable();
}

void lp_touch_key_charge_mode_enter()
{
    log_debug("%s", __func__);
    if (!__this->pdata) {
        return;
    }

    is_lpkey_active = 0;

    //复位算法
    if (__this->pdata->charge_enter_algo_reset) {
        lpctmu_send_m2p_cmd(RESET_IDENTIFY_ALGO);
    }

    if (__this->pdata->charge_mode_keep_touch) {
        if (__this->pdata->charge_enter_algo_reset) {
        } else {
            lp_touch_key_reset_algo();
        }
        return;
    }

    log_debug("lpctmu disable\n");
    lpctmu_disable();
}

void lp_touch_key_charge_mode_exit()
{
    log_debug("%s", __func__);
    if (!__this->pdata) {
        return;
    }

    is_lpkey_active = 0;

    //复位算法
    if (__this->pdata->charge_exit_algo_reset) {
        lp_touch_key_reset_algo();
    }

    if (__this->pdata->charge_mode_keep_touch) {
        return;
    }

    log_debug("lpctmu enable\n");
    lpctmu_enable();
}

void set_lpkey_active(u32 set)
{
    is_lpkey_active = set;
}

static u8 lpkey_idle_query(void)
{
    return !is_lpkey_active;
}

REGISTER_LP_TARGET(key_lp_target) = {
    .name = "lpkey",
    .is_idle = lpkey_idle_query,
};

#else

void lp_touch_key_event_irq_handler()
{
}

#endif


