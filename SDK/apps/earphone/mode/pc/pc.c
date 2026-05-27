#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".pc.data.bss")
#pragma data_seg(".pc.data")
#pragma const_seg(".pc.text.const")
#pragma code_seg(".pc.text")
#endif
#include "system/includes.h"
#include "app_action.h"
#include "audio_config.h"
#include "usb/device/usb_stack.h"
#include "usb/usb_task.h"
#include "usb/device/hid.h"
#include "usb/device/msd.h"
#include "uac_stream.h"
#include "pc.h"
#include "tone_player.h"
#include "app_tone.h"
#include "user_cfg.h"
#include "app_task.h"
#include "app_main.h"
#include "app_default_msg_handler.h"
#include "dev_manager.h"

#if ((TCFG_CHARGESTORE_ENABLE || TCFG_TEST_BOX_ENABLE || TCFG_ANC_BOX_ENABLE) \
     && TCFG_CHARGESTORE_PORT == IO_PORT_DP)
#include "chargestore/chargestore.h"
#endif

#define LOG_TAG             "[PC]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

#if TCFG_APP_PC_EN

struct pc_opr {
    u8 onoff;
    u8 pc_is_active;
    u8 prev_key_msg;
    u8 pp_wait_release;
    u16 key_hold_timer;
};

static struct pc_opr pc_hdl = {0};
#define __this 	(&pc_hdl)

extern void dac_try_power_on_thread();


/**
 * @brief pc 在线检测  切换模式判断使用
 * @return 1 pc设备在线 0 设备不在线
 */
static int app_pc_check(void)
{
#if TCFG_PC_BACKMODE_ENABLE
    return false;
#endif

    u32 r = usb_otg_online(0);
    log_info("pc_app_check %d", r);
    if ((r == SLAVE_MODE) ||
        (r == SLAVE_MODE_WAIT_CONFIRMATION)) {
        return true;
    }
    return false;
}

/**
 * @brief pc打开
 */
static void pc_task_start(void)
{
    if (__this->onoff) {
        log_info("PC is start ");
        return;
    }
    log_info("App Start - PC");

#if ((TCFG_CHARGESTORE_ENABLE || TCFG_TEST_BOX_ENABLE || TCFG_ANC_BOX_ENABLE) \
     && TCFG_CHARGESTORE_PORT == IO_PORT_DP)
    p33_io_wakeup_enable(TCFG_CHARGESTORE_PORT, 0);
    chargestore_api_stop();
#endif

#if TCFG_PC_ENABLE
    usb_message_to_stack(USBSTACK_START, 0, 1);
#endif

    __this->onoff = 1;
}

/**
 * @brief pc关闭
 */
static void pc_task_stop(void)
{
    if (!__this->onoff) {
        log_info("PC is stop ");
#if TCFG_PC_ENABLE
        usb_message_to_stack(USBSTACK_STOP, 0, 1);
#endif
        return ;
    }
    __this->onoff = 0;
    u32 state = usb_otg_online(0);
    if (state != SLAVE_MODE && state != SLAVE_MODE_WAIT_CONFIRMATION) {
        log_info("App Stop - PC");
#if TCFG_PC_ENABLE
        usb_message_to_stack(USBSTACK_STOP, 0, 1);
#endif
    } else {
        log_info("App Hold- PC");
#if TCFG_PC_ENABLE
        usb_message_to_stack(USBSTACK_PAUSE, 0, 1);
#endif
    }

#if ((TCFG_CHARGESTORE_ENABLE || TCFG_TEST_BOX_ENABLE || TCFG_ANC_BOX_ENABLE) \
     && TCFG_CHARGESTORE_PORT == IO_PORT_DP)
    chargestore_api_restart();
    p33_io_wakeup_enable(TCFG_CHARGESTORE_PORT, 1);
#endif

#if (TCFG_DEV_MANAGER_ENABLE)
    dev_manager_list_check_mount();
#endif/*TCFG_DEV_MANAGER_ENABLE*/

}

static int pc_tone_play_end_callback(void *priv, enum stream_event event)
{
    if (false == app_in_mode(APP_MODE_PC)) {
        return 0;
    }
    if (!__this->pc_is_active) {
        return 0;
    }
    switch (event) {
    case STREAM_EVENT_NONE:
    case STREAM_EVENT_STOP:
        pc_task_start();
        break;
    default:
        break;
    }
    return 0;
}

static int pc_mode_init()
{
    printf("pc mode\n");
    __this->pc_is_active = 1;
    tone_player_stop();
    int ret = play_tone_file_callback(get_tone_files()->pc_mode, NULL, pc_tone_play_end_callback);
    if (ret != 0) {
        log_error("pc tone play err!!!");
#if  TCFG_USB_SLAVE_AUDIO_SPK_ENABLE
        dac_try_power_on_thread();//dac初始化耗时有120ms,此处提前将dac指定到独立任务内做初始化，优化PC通路启动的耗时，减少时间戳超时的情况
#endif
        pc_task_start();
    }
    app_send_message(APP_MSG_ENTER_MODE, APP_MODE_PC);

    return 0;
}

static void pc_hid_hold_release(void *priv)
{
    //hid按键抬起
    hid_key_handler_send_one_packet(0, USB_AUDIO_NONE);
    __this->key_hold_timer = 0;
}

static void pc_hid_key_send_with_hold(int hid_key)
{
    if (__this->prev_key_msg != hid_key) {
        //如果上一个hid按键未抬起，先抬起它
        if (__this->key_hold_timer) {
            sys_timeout_del(__this->key_hold_timer);
            __this->key_hold_timer = 0;
            pc_hid_hold_release(0);
        }
    }
    if (__this->key_hold_timer == 0) {
        //如果未有hid按键按下，就发送按下，并设定超时时间，超时未收到
        //KEY_EVENT_HOLD（即按键hold消息），就发送hid按键抬起。hold消息一般是
        //每隔15 x 10 = 150ms发一次，所以这里设的200ms超时(大于150ms即可)，如
        //果修改了hold的时间间隔，这里的超时时间要对应修改
        hid_key_handler_send_one_packet(0, hid_key);
        __this->key_hold_timer = sys_timeout_add(0, pc_hid_hold_release, 200);
    } else {
        //超时时间内收到hold消息，重新开始计时，不发送hid按键抬起
        sys_timer_re_run(__this->key_hold_timer);
    }
    __this->prev_key_msg = hid_key;
}

static void pc_play_pause()
{
    hid_key_handler(0, USB_AUDIO_PP);
}

static void pc_music_prev()
{
    //android和ios三击PP上一曲，windows发标准的PREV
    if (usb_get_host_type(0) == HOST_TYPE_ANDROID ||
        usb_get_host_type(0) == HOST_TYPE_IOS) {
        hid_key_handler(0, USB_AUDIO_PP);
        os_time_dly(2);
        hid_key_handler(0, USB_AUDIO_PP);
        os_time_dly(2);
        hid_key_handler(0, USB_AUDIO_PP);
    } else {
        hid_key_handler(0, USB_AUDIO_PREFILE);
    }
}

static void pc_music_next()
{
    //android和ios双击PP下一曲，windows发标准的NEXT
    if (usb_get_host_type(0) == HOST_TYPE_ANDROID ||
        usb_get_host_type(0) == HOST_TYPE_IOS) {
        hid_key_handler(0, USB_AUDIO_PP);
        os_time_dly(2);
        hid_key_handler(0, USB_AUDIO_PP);
    } else {
        hid_key_handler(0, USB_AUDIO_NEXTFILE);
    }
}

static void pc_vol_up()
{
    //hid_key_handler(0, USB_AUDIO_VOLUP);
    //音量+带hold处理
    pc_hid_key_send_with_hold(USB_AUDIO_VOLUP);
    puts(">>>pc vol+\n");
}

static void pc_vol_down()
{
    //hid_key_handler(0, USB_AUDIO_VOLDOWN);
    //音量-带hold处理
    pc_hid_key_send_with_hold(USB_AUDIO_VOLDOWN);
    puts(">>>pc vol-");
}

static void pc_hid_pp_long_press_release(void *priv)
{
    hid_key_handler_send_one_packet(0, USB_AUDIO_NONE);
    __this->pp_wait_release = 0;
}

static void pc_call_reject_or_hand_up()
{
    if (__this->pp_wait_release) {
        //HID按键未释放不允许再次发送
        return;
    }
    //来电：
    //android   短按接听，长按拒接
    //ios       短按接听，长按拒接
    //通话中：
    //android   短按toggle mic mute，长按挂断
    //ios       短按挂断
    hid_key_handler_send_one_packet(0, USB_AUDIO_PP);
    sys_timeout_add(0, pc_hid_pp_long_press_release, 1200);
    __this->pp_wait_release = 1;
}


static void pc_app_msg_handler(int *msg)
{
    switch (msg[0]) {
    case APP_MSG_MUSIC_PP:
        pc_play_pause();
        break;
    case APP_MSG_MUSIC_NEXT:
        pc_music_next();
        break;
    case APP_MSG_MUSIC_PREV:
        pc_music_prev();
        break;
    case APP_MSG_VOL_UP:
        pc_vol_up();
        break;
    case APP_MSG_VOL_DOWN:
        pc_vol_down();
        break;
    }

}

static int pc_mode_try_enter(int arg)
{
    if (true == app_pc_check()) {
        return 0;
    }
    return 1;
}

/**
 * @brief pc模式退出
 */
int pc_mode_try_exit()
{
    pc_task_stop();
    __this->pc_is_active = 0;

    return 0;
}

static int pc_mode_exit()
{
    app_send_message(APP_MSG_EXIT_MODE, APP_MODE_PC);
    return 0;
}

struct app_mode *app_enter_pc_mode(int arg)
{
    int msg[16];
    struct app_mode *next_mode;

    pc_mode_init();

    while (1) {
        if (!app_get_message(msg, ARRAY_SIZE(msg), pc_mode_key_table)) {
            continue;
        }
        next_mode = app_mode_switch_handler(msg);
        if (next_mode) {
            break;
        }

        switch (msg[0]) {
        case MSG_FROM_APP:
            pc_app_msg_handler(msg + 1);
            break;
        case MSG_FROM_DEVICE:
            break;
        }

        app_default_msg_handler(msg);
    }

    pc_mode_exit();

    return next_mode;
}

static u8 pc_idle_query(void)
{
    return !__this->pc_is_active;
}

REGISTER_LP_TARGET(pc_lp_target) = {
    .name = "pc",
    .is_idle = pc_idle_query,
};

static const struct app_mode_ops pc_mode_ops = {
    .try_enter		= pc_mode_try_enter,
    .try_exit       = pc_mode_try_exit,
};

REGISTER_APP_MODE(pc_mode) = {
    .name   = APP_MODE_PC,
    .index  = APP_MODE_PC_INDEX,
    .ops    = &pc_mode_ops,
};

#endif
