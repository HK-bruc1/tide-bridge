# Phase 6 C1 实施文档：连接与模式状态机收口

> **目标：** 在 `rdx_ble_server.c` 内部建立私有的 BLE 模式控制器，把 `hogp_mode` 与连接归属解耦，解决异步断连后事件错分问题，为 RDX App Config 与 HOGP 提供统一的切换入口。
> **适用分支：** `HOGP`
> **前置阶段：** Phase 6 C2 已完成并提交
> **冻结契约：** Profile v1（HID Service `0x0016-0x0022`、70 字节 Report Map、8 字节 Input Report）保持不变

---

## 1. 范围

本次修改的文件：

- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c`（主要实现）
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h`（仅增加两个窄公共 wrapper 声明）
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_config.h`（增加调试宏默认值）
- `SDK/apps/common/third_party_profile/rdx_protocol/rdx_hogp_keyboard.c`（仅把 NUM0 调试入口和 key send 改为调用公共 wrapper）
- `tests/host/test_hogp_profile_contract.ps1`（扩展静态检查）

不修改 `rdx_app.c`，不移动 Output Report handle，不修改 Profile v1 字节表，不删除现有耳机主路径。

---

## 2. 当前基线

C2 完成后：

- `hogp_mode_get()` 仍决定 HCI 连接/断连事件是否转发给 HOGP。
- `rdx_ble_server_info.ble_conn` 与 HOGP 连接状态未同步，模式切换可能未断连就恢复 RDX 广播。
- Output Report 已受 `TCFG_RDX_HOGP_ENABLE` 门控；`rdx_hogp_deinit()` 在 Server exit 前被调用。

C1 要在此基础上增加显式状态机，使连接归属不再依赖 `hogp_mode`。

---

## 3. 设计原则

1. **状态机完全私有**：所有模式/owner 字段放在 `rdx_ble_server.c` 的 `static` 结构体中，不进 `rdx_ble_server_info_t` 公共头。
2. **最小公共接口**：只暴露两个 wrapper：`rdx_ble_mode_request_hogp()` 和 `rdx_ble_connection_owner_is_hogp()`。
3. **广播切换不互相覆盖**：HOGP 模式启动 HID 广播；CONFIG 模式停止 HID 广播并恢复 RDX 广播，两者不通过同一个 `rdx_ble_server_adv_data_changed()` 强制刷新。
4. **断连清理与广播恢复拆开**：Config owner 断连时先只做清理，应用 pending 模式后再决定是否恢复 RDX 广播。
5. **owner 授权双向覆盖**：HOGP owner 拒绝 RDX App 写/notify；CONFIG owner 拒绝 HID Service 和 Output Report 写/notify。

---

## 4. 私有状态机定义

在 `rdx_ble_server.c` 的宏定义区之后、全局/局部变量区之前添加：

```c
typedef enum {
    RDX_BLE_MODE_CONFIG = 0,
    RDX_BLE_MODE_HOGP,
} rdx_ble_mode_t;

typedef enum {
    RDX_BLE_OWNER_NONE = 0,
    RDX_BLE_OWNER_CONFIG,
    RDX_BLE_OWNER_HOGP,
} rdx_ble_connection_owner_t;

typedef struct {
    rdx_ble_mode_t requested_mode;
    rdx_ble_mode_t advertised_mode;
    rdx_ble_connection_owner_t connection_owner;
    u8 switch_pending;
} rdx_ble_mode_controller_t;

static rdx_ble_mode_controller_t s_ble_mode = {
    .requested_mode = RDX_BLE_MODE_CONFIG,
    .advertised_mode = RDX_BLE_MODE_CONFIG,
    .connection_owner = RDX_BLE_OWNER_NONE,
    .switch_pending = 0,
};
```

C1 阶段默认模式保持 `RDX_BLE_MODE_CONFIG`，与当前上电行为一致；C5 再把默认改成 HOGP。

---

## 5. 私有辅助函数

在 `rdx_ble_server.c` 中新增：

```c
static const char *rdx_ble_owner_name(rdx_ble_connection_owner_t owner)
{
    switch (owner) {
    case RDX_BLE_OWNER_NONE:   return "NONE";
    case RDX_BLE_OWNER_CONFIG: return "CONFIG";
    case RDX_BLE_OWNER_HOGP:   return "HOGP";
    default:                   return "UNKNOWN";
    }
}

static const char *rdx_ble_mode_name(rdx_ble_mode_t mode)
{
    switch (mode) {
    case RDX_BLE_MODE_CONFIG: return "CONFIG";
    case RDX_BLE_MODE_HOGP:   return "HOGP";
    default:                  return "UNKNOWN";
    }
}

static void rdx_ble_mode_controller_dump(const char *prefix)
{
    y_printf("[BLE_MODE] %s req=%s adv=%s owner=%s pending=%d con=0x%04x conn=%d\n",
             prefix ? prefix : "",
             rdx_ble_mode_name(s_ble_mode.requested_mode),
             rdx_ble_mode_name(s_ble_mode.advertised_mode),
             rdx_ble_owner_name(s_ble_mode.connection_owner),
             s_ble_mode.switch_pending,
             g_rdx_ble_server_info.ble_con_handle,
             g_rdx_ble_server_info.ble_conn);
}

static void rdx_ble_mode_controller_init(void)
{
    s_ble_mode.requested_mode = RDX_BLE_MODE_CONFIG;
    s_ble_mode.advertised_mode = RDX_BLE_MODE_CONFIG;
    s_ble_mode.connection_owner = RDX_BLE_OWNER_NONE;
    s_ble_mode.switch_pending = 0;
}

static void rdx_ble_mode_controller_reset(void)
{
    rdx_ble_mode_controller_init();
}
```

---

## 6. 广播启停辅助

```c
/* 该函数定义在 rdx_ble_server.c 后部，此处仅作本文件内前向声明 */
extern void rdx_ble_server_adv_interval_change_timer_stop(void);

static u8 rdx_ble_mode_broadcast_suppressed(void)
{
    RdxWifiInfo* k = rdx_app_get_wifi_info();
    bool rdx_uxfile_sd_format_status_check(void);

    if (k->onoff == TRANSFER_BY_WIFI_ON) {
        return 1;
    }

    if (rdx_app_get_poweroff_flag() ||
        rdx_app_get_dut_status() ||
        rdx_uxfile_sd_format_status_check()) {
        return 1;
    }

    return 0;
}

static void rdx_ble_mode_start_config_advertising(void)
{
    /* 先停止当前广播（可能是 HID 数据），再用 RDX 数据重新使能，
     * 确保从 HOGP 切回 CONFIG 时空口立即恢复为 RDX 广播。 */
    rdx_ble_server_adv_enable(0);
    rdx_ble_server_adv_enable(1);
}

static void rdx_ble_mode_start_hogp_advertising(void)
{
    rdx_ble_server_adv_interval_change_timer_stop();
#if TCFG_RDX_HOGP_ENABLE
    hogp_mode_set(1);
#else
    /* HOGP 编译关闭时无法真正广播为 HOGP，回退到 RDX 广播 */
    rdx_ble_server_adv_enable(1);
#endif
}

static void rdx_ble_mode_restart_hogp_advertising(void)
{
    if (rdx_ble_mode_broadcast_suppressed()) {
        rdx_ble_server_adv_enable(0);
        return;
    }

    rdx_ble_server_adv_interval_change_timer_stop();
#if TCFG_RDX_HOGP_ENABLE
    if (hogp_mode_get()) {
        rdx_hogp_adv_start();
    } else {
        hogp_mode_set(1);
    }
#else
    rdx_ble_server_adv_enable(1);
#endif
}

static void rdx_ble_mode_sync_hogp_runtime(void)
{
#if TCFG_RDX_HOGP_ENABLE
    if (s_ble_mode.advertised_mode == RDX_BLE_MODE_CONFIG) {
        if (hogp_mode_get()) {
            rdx_hogp_runtime_cleanup();
        }
    }
#endif
}
```

说明：
- `rdx_ble_mode_broadcast_suppressed()` 是广播抑制的唯一入口，统一判断 DUT、关机、WiFi 传输、SD 格式化四种场景。
- `restart_hogp_advertising()` 在抑制状态下只关闭广播，不重启 HID 广播；普通模式切换在 `apply_internal()` 的 `start_adv` 分支入口也受同一条件保护，避免 NUM0 短按等无连接场景下短暂启播。
- 切到 HOGP 前停止 `adv_interval_change_timer`，避免 RDX 的慢速广播 timer 后续操作同一个 `app_ble` handle。
- `rdx_hogp_runtime_cleanup()` 是 HOGP 子模块暴露的纯 runtime 清理接口，只复位 `s_hogp_mode` / 连接状态 / 输入报告缓冲等，不启停任何广播。
- `rdx_ble_mode_sync_hogp_runtime()` 与启停广播完全解耦：当目标身份切到 CONFIG 时调用 `rdx_hogp_runtime_cleanup()` 清理 HOGP runtime，真正的广播恢复仍由 server 断连策略决定。
- 如果把这些 helper 移到 `rdx_ble_server_adv_interval_change_timer_stop()` 定义之后，可去掉前向声明。

---

## 7. 模式请求与应用

```c
static void rdx_ble_mode_apply_requested_internal(u8 force, u8 start_adv)
{
    if (!s_ble_mode.switch_pending) {
        return;
    }

    if (!force &&
        (g_rdx_ble_server_info.ble_conn ||
         app_ble_get_hdl_con_handle(g_rdx_ble_server_info.rdx_ble_server_hdl))) {
        y_printf("[BLE_MODE] apply postponed: connection still active\n");
        return;
    }

    s_ble_mode.switch_pending = 0;

    if (s_ble_mode.requested_mode == s_ble_mode.advertised_mode) {
        y_printf("[BLE_MODE] no broadcast change needed\n");
        return;
    }

    s_ble_mode.advertised_mode = s_ble_mode.requested_mode;
    rdx_ble_mode_controller_dump("apply");

    rdx_ble_mode_sync_hogp_runtime();

    if (start_adv) {
        /* DUT、poweroff、WiFi 传输、SD 格式化期间抑制所有广播活动：
         * 已迁移的模式状态（requested/advertised）保留，但射频保持关闭。 */
        if (rdx_ble_mode_broadcast_suppressed()) {
            y_printf("[BLE_MODE] mode switch suppressed, advertising disabled\n");
            rdx_ble_server_adv_enable(0);
            return;
        }

        if (s_ble_mode.advertised_mode == RDX_BLE_MODE_HOGP) {
            rdx_ble_mode_start_hogp_advertising();
        } else {
            rdx_ble_mode_start_config_advertising();
        }
    }
}

static void rdx_ble_mode_apply_requested(void)
{
    rdx_ble_mode_apply_requested_internal(0, 1);
}

static void rdx_ble_mode_apply_requested_force(void)
{
    rdx_ble_mode_apply_requested_internal(1, 0);
}

static int rdx_ble_mode_request(rdx_ble_mode_t mode)
{
    if (mode != RDX_BLE_MODE_CONFIG && mode != RDX_BLE_MODE_HOGP) {
        y_printf("[BLE_MODE] invalid mode request %d\n", mode);
        return -1;
    }

    if (s_ble_mode.requested_mode == mode && !s_ble_mode.switch_pending) {
        y_printf("[BLE_MODE] mode %s already requested, ignore\n", rdx_ble_mode_name(mode));
        return 0;
    }

    s_ble_mode.requested_mode = mode;
    s_ble_mode.switch_pending = 1;
    rdx_ble_mode_controller_dump("request");

    if (g_rdx_ble_server_info.ble_conn ||
        app_ble_get_hdl_con_handle(g_rdx_ble_server_info.rdx_ble_server_hdl)) {
        y_printf("[BLE_MODE] active connection, disconnect before switch\n");
        rdx_ble_server_app_disconnect();
        return 0;
    }

    rdx_ble_mode_apply_requested();
    return 0;
}
```

---

## 8. 公共 wrapper

在 `rdx_ble_server.h` 中添加声明：

```c
void rdx_ble_mode_request_hogp(u8 enable);
u8   rdx_ble_connection_owner_is_hogp(void);
```

在 `rdx_ble_server.c` 中实现：

```c
void rdx_ble_mode_request_hogp(u8 enable)
{
#if TCFG_RDX_HOGP_ENABLE
    rdx_ble_mode_request(enable ? RDX_BLE_MODE_HOGP : RDX_BLE_MODE_CONFIG);
#else
    (void)enable;
#endif
}

u8 rdx_ble_connection_owner_is_hogp(void)
{
    return (s_ble_mode.connection_owner == RDX_BLE_OWNER_HOGP) ? 1 : 0;
}
```

---

## 9. 初始化与复位

在 `rdx_ble_server_init()` 中，于 `rdx_hogp_init(...)` 之前调用：

```c
    rdx_ble_mode_controller_init();
```

在 `rdx_ble_server_exit()` 中，于 `app_ble_hdl_free()` 之前调用：

```c
    rdx_hogp_deinit();
    rdx_ble_mode_controller_reset();
```

---

## 10. HCI 连接完成事件

### 10.1 普通连接完成

替换 `HCI_SUBEVENT_LE_CONNECTION_COMPLETE` 分支：

```c
                    case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                        con_handle = little_endian_read_16(packet, 4);
                        log_info("HCI_SUBEVENT_LE_CONNECTION_COMPLETE: %0x", con_handle);

                        rdx_ble_server_set_conn_handle(con_handle);
                        g_rdx_ble_server_info.ble_conn = TRUE;

                        if (s_ble_mode.advertised_mode == RDX_BLE_MODE_HOGP) {
                            s_ble_mode.connection_owner = RDX_BLE_OWNER_HOGP;
                        } else {
                            s_ble_mode.connection_owner = RDX_BLE_OWNER_CONFIG;
                        }
                        rdx_ble_mode_controller_dump("connected");

#if TCFG_RDX_HOGP_ENABLE
                        if (s_ble_mode.connection_owner == RDX_BLE_OWNER_HOGP) {
                            rdx_hogp_on_connected(con_handle);
                        }
#endif

                        if (s_ble_mode.connection_owner == RDX_BLE_OWNER_CONFIG) {
                            rdx_ble_server_set_ble_work_state(BLE_ST_CONNECT);
                            rdx_ble_server_reset_send_fail_cnt();
                            rdx_ble_server_connection_update_complete_success(packet + 8);
                            put_buf(&packet[8], 6);
                            att_server_set_exchange_mtu(con_handle);
                            rdx_ble_server_connected_handle();
                        }
                        break;
```

### 10.2 增强连接完成

只处理 HOGP owner 转发，不执行 RDX 初始化：

```c
                    case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE:
                        {
                            r_printf("---------> HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE \n");
                            con_handle = little_endian_read_16(packet, 4);
                            log_info("HCI_SUBEVENT_LE_CONNECTION_COMPLETE: %0x", con_handle);

                            rdx_ble_server_set_conn_handle(con_handle);
                            g_rdx_ble_server_info.ble_conn = TRUE;

                            if (s_ble_mode.advertised_mode == RDX_BLE_MODE_HOGP) {
                                s_ble_mode.connection_owner = RDX_BLE_OWNER_HOGP;
                            } else {
                                s_ble_mode.connection_owner = RDX_BLE_OWNER_CONFIG;
                            }
                            rdx_ble_mode_controller_dump("connected(enhanced)");

#if TCFG_RDX_HOGP_ENABLE
                            if (s_ble_mode.connection_owner == RDX_BLE_OWNER_HOGP) {
                                rdx_hogp_on_connected(con_handle);
                            }
#endif
                        }
                        break;
```

---

## 11. HCI 断连事件

先拆分 `rdx_ble_server_disconnected_handle()`：

```c
static void rdx_ble_server_disconnected_cleanup_internal(void)
{
    /* 原 rdx_ble_server_disconnected_handle() 中除 adv restart 之外的全部内容 */
    RdxWifiInfo* k = rdx_app_get_wifi_info();
    RecordStatus* rp = rdx_record_get_status();

    rdx_ble_server_stop_force_disconnect_timer();

    if(g_syn_data_timer) {
        sys_timeout_del(g_syn_data_timer);
        g_syn_data_timer = 0;
    }

    g_rdx_ble_server_info.ble_mtu_size = 0;
    g_rdx_ble_server_info.ccc_configured = FALSE;
    g_rdx_ble_server_info.stream_tx_ready = FALSE;

    if(k->onoff != TRANSFER_BY_WIFI_ON){
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_DISCONNECTED);
    }

    rdx_record_stream_interrupt();
    rdx_record_on_ble_conn_changed(false);

#if (RDX_AI_SEL_APP & APP_NINGQU_EN) || ... /* 保留原有宏 */
    if(rp->orig_mode != RECORD_MODE_OFFLINE){
        rp->mode = RECORD_MODE_OFFLINE;
        rp->orig_mode = RECORD_MODE_OFFLINE;
    }
#else
    if(rp->orig_mode != RECORD_MODE_OFFLINE){
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME){
            rp->run = RECORD_STATE_STOP;
        #if !(RDX_AI_SEL_APP & APP_TURING_EN)
            rp->rerun = true;
        #endif
            rdx_record_process();
        }
    }
#endif

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)
    rdx_app_tws_bind_info_sync();
#endif

    if (get_ota_status()){
        rdx_ota_stop();
    }

    sys_timeout_add(NULL, rdx_ble_server_disconnected_delay_handle, 500);
}

static void rdx_ble_server_disconnected_adv_restart(void)
{
    /* 原 rdx_ble_server_disconnected_handle() 最后的 adv restart 块 */
    RdxWifiInfo* k = rdx_app_get_wifi_info();
    bool rdx_uxfile_sd_format_status_check(void);

    if(k->onoff == TRANSFER_BY_WIFI_ON){
        rdx_ble_server_adv_enable(0);
    } else {
        bool flag = rdx_app_get_dut_status();
        if(rdx_app_get_poweroff_flag() || flag == true || rdx_uxfile_sd_format_status_check()){
            rdx_ble_server_adv_enable(0);
        } else {
            rdx_ble_server_adv_data_changed();
            rdx_ble_server_auto_shut_down_enable(1);
        }
        if(!rdx_vm_is_unbouding()){
        #if (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_OLED) || (RDX_MULTI_FUNC_INTERFACE == RDX_SUPPORT_BOTH_OLED_EMMC)
            r_printf("=== %s ---> do not show disconnect icon, unbounding now! \r", __FUNCTION__);
        #endif
        }
    }
}
```

然后把 `rdx_ble_server_disconnected_handle()` 改为：

```c
void rdx_ble_server_disconnected_handle(void)
{
    g_rdx_ble_server_info.ble_conn = FALSE;
    rdx_ble_server_set_ble_work_state(BLE_ST_DISCONN);
    rdx_ble_server_disconnected_cleanup_internal();
    rdx_ble_server_disconnected_adv_restart();
}
```

最后替换 `HCI_EVENT_DISCONNECTION_COMPLETE` 分支：

```c
            case HCI_EVENT_DISCONNECTION_COMPLETE:
                {
                    log_info("HCI_EVENT_DISCONNECTION_COMPLETE: %0x", packet[5]);
                    con_handle = 0;
                    rdx_ble_server_set_conn_handle(con_handle);
                    g_rdx_ble_server_info.ble_conn = FALSE;
                    rdx_ble_server_set_ble_work_state(BLE_ST_DISCONN);

                    rdx_ble_connection_owner_t prev_owner = s_ble_mode.connection_owner;
                    rdx_ble_mode_controller_dump("disconnecting");

#if TCFG_RDX_HOGP_ENABLE
                    if (prev_owner == RDX_BLE_OWNER_HOGP) {
                        rdx_hogp_on_disconnected(con_handle);
                    }
#endif

                    if (prev_owner == RDX_BLE_OWNER_CONFIG) {
                        rdx_ble_server_reset_send_fail_cnt();
                        rdx_record_stream_interrupt();
                        rdx_ble_server_disconnected_cleanup_internal();
                    }

                    s_ble_mode.connection_owner = RDX_BLE_OWNER_NONE;
                    rdx_ble_mode_controller_dump("disconnected");

                    /* 断连回调期间 wrapper handle 可能仍是旧连接，但 HCI 事件本身已保证连接结束。
                     * 使用 force 路径直接迁移 advertised_mode，广播重启由下方代码负责。 */
                    rdx_ble_mode_apply_requested_force();

                    /* 断连后保持当前广播身份：HOGP 强制重启 HID 广播，CONFIG 恢复 RDX 广播 */
                    if (s_ble_mode.advertised_mode == RDX_BLE_MODE_HOGP) {
                        rdx_ble_mode_restart_hogp_advertising();
                    } else {
                        rdx_ble_server_disconnected_adv_restart();
                    }
                }
                break;
```

---

## 12. Owner 授权

### 12.1 HID 读取

在 `rdx_ble_server_att_read_callback()` 中：

```c
        case HID_PROTOCOL_MODE_VALUE_HANDLE:
        case HID_REPORT_MAP_VALUE_HANDLE:
        case HID_INFORMATION_VALUE_HANDLE:
        case HID_INPUT_REPORT_VALUE_HANDLE:
        case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
#if TCFG_RDX_HOGP_ENABLE
            if (!rdx_ble_connection_owner_is_hogp()) {
                y_printf("[HOGP] read rejected: owner=%s\n",
                         rdx_ble_owner_name(s_ble_mode.connection_owner));
                break;
            }
            att_value_len = rdx_hogp_att_read(connection_handle, handle, offset, buffer, buffer_size);
            if (att_value_len) {
                y_printf("[HOGP] read hdl=0x%04x offset=%d len=%d\r", handle, offset, att_value_len);
            }
#endif
            break;
```

### 12.2 HID 与 RDX 写入

在 `rdx_ble_server_att_write_callback()` 中，把现有的 HOGP 路由替换为：

```c
#if TCFG_RDX_HOGP_ENABLE
    /* HID Service handles: only HOGP owner */
    if (handle >= HID_SERVICE_START_HANDLE && handle <= HID_SERVICE_END_HANDLE) {
        if (!rdx_ble_connection_owner_is_hogp()) {
            y_printf("[HOGP] write rejected: owner=%s hdl=0x%04x\n",
                     rdx_ble_owner_name(s_ble_mode.connection_owner), handle);
            return 0;
        }
        return rdx_hogp_att_write(connection_handle, handle, transaction_mode, offset, buffer, buffer_size);
    }

    /* Output Report handle: only HOGP owner */
    if (handle == HID_OUTPUT_REPORT_VALUE_HANDLE) {
        if (!rdx_ble_connection_owner_is_hogp()) {
            y_printf("[HOGP] output report write rejected: owner=%s\n",
                     rdx_ble_owner_name(s_ble_mode.connection_owner));
            return 0;
        }
        if (buffer_size >= 1) {
            y_printf("[HOGP] output report write, LED=0x%02x\r", buffer[0]);
        }
        return 0;
    }
#endif

    /* RDX App Config handles: only CONFIG owner */
    if (handle == ATT_CHARACTERISTIC_06068D1C_6B97_11EF_B864_0241AC120002_01_VALUE_HANDLE ||
        handle == ATT_CHARACTERISTIC_00239A7F_C616_89BB_3374_F15AF588A7B3_01_VALUE_HANDLE) {
        if (s_ble_mode.connection_owner != RDX_BLE_OWNER_CONFIG) {
            y_printf("[BLE_MODE] RDX write rejected: owner=%s hdl=0x%04x\n",
                     rdx_ble_owner_name(s_ble_mode.connection_owner), handle);
            return 0;
        }
    }

    if (handle == ATT_CHARACTERISTIC_06068D2C_6B97_11EF_B864_0242AC120002_01_CLIENT_CONFIGURATION_HANDLE ||
        handle == ATT_CHARACTERISTIC_00239A8F_C616_89BB_3374_F25AF588A7B3_01_CLIENT_CONFIGURATION_HANDLE) {
        if (s_ble_mode.connection_owner != RDX_BLE_OWNER_CONFIG) {
            y_printf("[BLE_MODE] RDX CCC write rejected: owner=%s hdl=0x%04x\n",
                     rdx_ble_owner_name(s_ble_mode.connection_owner), handle);
            return 0;
        }
    }
```

说明：
- HID 只允许 `OWNER_HOGP`。
- RDX App 写/CCC 只允许 `OWNER_CONFIG`。
- `OWNER_NONE` 时全部拒绝并打印日志，与 Phase 6 总方案不变量一致。

### 12.3 RDX notify 发送路径

`rdx_ble_server_send()` 和 `rdx_ble_server_ota_send()` 直接操作 RDX App notify handle，必须在开头增加 owner 检查：

```c
int rdx_ble_server_send(u8 *data, u32 len)
{
    if (s_ble_mode.connection_owner != RDX_BLE_OWNER_CONFIG) {
        y_printf("[BLE_MODE] RDX server send rejected: owner=%s\n",
                 rdx_ble_owner_name(s_ble_mode.connection_owner));
        g_ble_send_fail_cnt++;
        return -1;
    }
    ...
}

int rdx_ble_server_ota_send(u8 *data, u32 len)
{
    if (s_ble_mode.connection_owner != RDX_BLE_OWNER_CONFIG) {
        y_printf("[BLE_MODE] RDX OTA send rejected: owner=%s\n",
                 rdx_ble_owner_name(s_ble_mode.connection_owner));
        return -1;
    }
    ...
}
```

这样 HOGP owner 下即使 RDX 业务层误调用发送，也不会把数据包发到 RDX notify handle。

---

## 13. HOGP key send 增加 owner 检查

在 `rdx_hogp_key_send_usage()` 的现有检查之后、发送之前增加：

```c
    if (!rdx_ble_connection_owner_is_hogp()) {
        RDX_HOGP_ERROR("key_send skipped: not HOGP owner");
        rdx_hogp_dump_state();
        return -1;
    }
```

此函数在 `rdx_hogp_keyboard.c` 中，只调用公共 wrapper `rdx_ble_connection_owner_is_hogp()`，不引用私有 enum。

---

## 14. 调试入口 NUM0

在 `rdx_hogp_config.h` 中新增：

```c
#ifndef RDX_BLE_DEBUG_MODE_SWITCH_KEY
#define RDX_BLE_DEBUG_MODE_SWITCH_KEY   1
#endif
```

修改 `rdx_hogp_on_io_num_key()`：

```c
int rdx_hogp_on_io_num_key(u8 num_idx, u8 action)
{
#if RDX_BLE_DEBUG_MODE_SWITCH_KEY
    if (num_idx == 0 && action == KEY_ACTION_CLICK && !s_hogp_mode) {
        rdx_ble_mode_request_hogp(1);
        RDX_HOGP_LOG("enter HOGP mode");
        return 0;
    }
    if (num_idx == 0 && action == KEY_ACTION_LONG && s_hogp_mode) {
        rdx_ble_mode_request_hogp(0);
        RDX_HOGP_LOG("exit HOGP mode");
        return 0;
    }
#endif

    if (!s_hogp_mode) {
        return -1;
    }

    if (action == KEY_ACTION_CLICK) {
        return rdx_hogp_key_click_index(num_idx - 1);
    }

    return -1;
}
```

C5 产品化时把 `RDX_BLE_DEBUG_MODE_SWITCH_KEY` 默认设为 0 或删除该入口。

---

## 15. Host 契约测试扩展

在 `tests/host/test_hogp_profile_contract.ps1` 中新增以下静态检查：

1. `rdx_ble_server.c` 中存在私有模式控制器（如 `static rdx_ble_mode_controller_t s_ble_mode` 或等效字段）。
2. `rdx_ble_server.h` 中声明了 `rdx_ble_mode_request_hogp` 和 `rdx_ble_connection_owner_is_hogp`。
3. `rdx_ble_server_info_t` 中没有新增 `rdx_ble_mode_t` / `rdx_ble_connection_owner_t` 字段。
4. `HCI_EVENT_DISCONNECTION_COMPLETE` 分支中，owner 清理之后调用 `rdx_ble_mode_apply_requested`（或等效函数名），并在最终为 HOGP 模式时调用 restart helper。
5. `rdx_ble_server_exit()` 中调用模式控制器复位。
6. `rdx_ble_server_att_write_callback()` 中对 `HID_OUTPUT_REPORT_VALUE_HANDLE` 有 owner 检查。
7. `rdx_ble_server_send()` 和 `rdx_ble_server_ota_send()` 中有 `connection_owner != RDX_BLE_OWNER_CONFIG` 拒绝逻辑。
8. RDX App 写/CCC 检查使用 `!= RDX_BLE_OWNER_CONFIG`（即同时拒绝 `OWNER_NONE` 和 `OWNER_HOGP`），而不是仅判断 `== OWNER_HOGP`。
9. `rdx_ble_mode_broadcast_suppressed()` 存在且检查 DUT / 关机 / WiFi 传输 / SD 格式化状态；`rdx_ble_mode_restart_hogp_advertising()` 和 `rdx_ble_mode_apply_requested_internal()` 的 `start_adv` 分支在抑制时只关闭广播、不调用启播 API。
10. `rdx_ble_server_adv_data_changed()` 在 `advertised_mode == RDX_BLE_MODE_HOGP` 时走 HOGP 广播重启，否则写 RDX 广播。
11. DUT 进入/退出时先调用 `rdx_ble_mode_request_hogp(0)` 再刷新广播。

这些检查通过正则/字符串匹配实现，不依赖构建。

---

## 16. 验证

### 16.1 主机测试

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tests/host/run_host_tests.ps1
```

如果 runner 找不到 `powershell.exe`，直接运行单测：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tests/host/test_t2620_config_overlay.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File tests/host/test_hogp_profile_contract.ps1
```

预期：全部通过，新增 C1 静态检查通过。

### 16.2 固件构建

```bash
cd SDK
make clean
make
```

预期：开启 HOGP 时编译通过。

### 16.3 关闭态构建

临时修改 `SDK/apps/earphone/include/t2620_project_config.h`：

```c
#define TCFG_RDX_HOGP_ENABLE 0
```

然后：

```bash
make clean && make
```

验证通过后恢复为 1。

### 16.4 上机验证

1. 上电默认 RDX 广播（C1 不改默认模式）。
2. NUM0 短按请求进入 HOGP，PC 发现设备并连接，A-D 输入正常。
3. NUM0 长按请求退出 HOGP，PC 断连，恢复 RDX 广播。
4. RDX App 连接后，HOGP handle 和 Output Report 写入被拒绝。
5. HOGP 连接后，RDX App 配置写/CCC 写入被拒绝。
6. 模式切换 100 次无残留连接、无旧 timer/handle 访问。

---

## 17. 风险与注意事项

- **C1 与 C2 的衔接**：C2 的 `hogp_mode_set()` 仍被 C1 调用，用于启停 HID 广播；`hogp_mode` 只表示 HID 广播状态，不再用于判断事件路由。
- **断连期间的模式请求**：`rdx_ble_mode_request()` 在连接存在时只记录请求并断连，实际广播切换延迟到 `HCI_EVENT_DISCONNECTION_COMPLETE` 之后。
- **断连后 HOGP 广播重播**：PC 主动断开 HOGP 时，`advertised_mode` 仍为 HOGP，由 `rdx_ble_mode_restart_hogp_advertising()` 强制重启 HID 广播。
- **增强连接事件**：只对 HOGP owner 做转发；RDX App Config 的完整初始化只在普通 `HCI_SUBEVENT_LE_CONNECTION_COMPLETE` 执行。需上机确认当前 SDK 是否会用 `HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE` 连接 RDX Config，如实际路径经过该事件，则需要补充 RDX 初始化逻辑。
- **ATT error code**：C1 只通过日志拒绝跨 owner 访问，不返回 ATT error code；C4 会统一处理。
- **默认模式**：C1 保持 `RDX_BLE_MODE_CONFIG` 为默认，避免 C5 的产品身份迁移提前生效。
- **HFP 共存**：C1 不处理 HFP 与 HOGP 的音频抢占，仅记录为已知风险。
- **广播抑制统一入口**：`rdx_ble_mode_broadcast_suppressed()` 在 `apply_internal()` 的 `start_adv` 分支入口和 `restart_hogp_advertising()` 中统一生效，避免 DUT、关机、WiFi 传输或 SD 格式化期间普通模式请求意外启播。
- **断连后广播恢复**：HOGP 模式调用 `rdx_ble_mode_restart_hogp_advertising()`；CONFIG 模式调用 `rdx_ble_server_disconnected_adv_restart()`，避免切换时先闪一下 RDX 广播。
- **RDX notify 授权**：`rdx_ble_server_send()` 和 `rdx_ble_server_ota_send()` 在 `connection_owner != CONFIG` 时拒绝发送，防止 HOGP owner 下仍有 RDX 业务包发出。
- **OWNER_NONE 全拒绝**：HID 只接受 OWNER_HOGP，RDX App 写/CCC/notify 只接受 OWNER_CONFIG，与 Phase 6 总方案不变量一致。
- **文件换行**：`rdx_ble_server.c` 原文件为 CRLF 换行；实施时只修改必要行，不要整体转换换行符，避免 diff 噪音。提交前用 `git diff --check` 检查空白错误。

---

## 18. 提交信息

```
fix(hogp): Phase 6 C1 连接与模式状态机收口

- 在 rdx_ble_server.c 内部新增私有 BLE 模式控制器：
  requested_mode、advertised_mode、connection_owner、switch_pending。
- 状态字段使用 static 结构体，不进 rdx_ble_server_info_t 公共头。
- 连接完成时根据 advertised_mode 锁定 connection_owner，不再使用
  hogp_mode_get() 判断事件归属。
- 断连事件按原 owner 分发清理（HOGP 或 RDX App Config），清理后再
  清空 owner 并应用挂起的模式请求。
- 模式切换请求在物理连接存在时先断连，切换延迟到断连完成后执行；
  HOGP 与 CONFIG 广播启停互不覆盖。
- ATT read/write、Output Report 写和 HOGP key send 增加 owner 授权；
  HOGP owner 下拒绝 RDX App 配置写/CCC/notify，CONFIG owner 下拒绝 HID 写入。
- 在 rdx_ble_server_send() 和 rdx_ble_server_ota_send() 增加 owner 检查，
  非 CONFIG owner 时拒绝 RDX notify。
- 拆分 rdx_ble_server_disconnected_handle() 为清理与广播恢复两部分；
  HOGP 断连后调用 restart_hogp_advertising() 强制重启 HID 广播，避免 PC
  主动断开后搜不到键盘。
- 切到 HOGP 前停止 adv_interval_change_timer，避免 RDX 慢速广播 timer
  影响 HOGP 广播间隔。
- rdx_ble_server_exit() 清空模式控制器状态。
- 保留 RDX_BLE_DEBUG_MODE_SWITCH_KEY 宏控制的 NUM0 调试入口。
- 扩展 host 契约测试，检查私有状态机、公共 wrapper 和 owner 授权逻辑。

不改变 Profile v1 外部契约：HID Service 0x0016-0x0022、70-byte Report
Map、8-byte Input Report 保持不变。
```

---

## 19. 完成后状态

C1 完成后，后续 C3/C4 可以安全地：

- 移除 `hogp_mode_get()` 在事件路由中的剩余使用；
- 把物理键入口从 HOGP 模块迁出；
- 在 ATT callback 中返回明确的 ATT error code；
- 冻结 `rdx_ble_mode_request_hogp()` 等接口供 RDX App 按键设置使用。
