
# make 编译并下载
# make VERBOSE=1 显示编译详细过程
# make clean 清除编译临时文件
#
# 注意： Linux 下编译方式：
#     1. 从 http://pkgman.jieliapp.com/doc/all 处找到下载链接
#     2. 下载后，解压到 /opt/jieli 目录下，保证
#       /opt/jieli/common/bin/clang 存在（注意目录层次）
#     3. 确认 ulimit -n 的结果足够大（建议大于8096），否则链接可能会因为打开文件太多而失败
#       可以通过 ulimit -n 8096 来设置一个较大的值
#

# 工具路径设置
ifeq ($(OS), Windows_NT)
# Windows 下工具链位置
TOOL_DIR := C:/JL/pi32/bin
CC    := clang.exe
CXX   := clang.exe
LD    := pi32v2-lto-wrapper.exe
AR    := llvm-ar.exe
MKDIR := mkdir_win -p
RM    := rm -rf

SYS_LIB_DIR := C:/JL/pi32/pi32v2-lib/r3-large
SYS_INC_DIR := C:/JL/pi32/pi32v2-include
EXT_CFLAGS  := # Windows 下不需要 -D__SHELL__
export PATH:=$(TOOL_DIR);$(PATH)

## 后处理脚本
FIXBAT          := tools/utils/fixbat.exe # 用于处理 utf8->gbk 编码问题
POST_SCRIPT     := cpu/br28/tools/download.bat
RUN_POST_SCRIPT := cpu\br28\tools\download.bat
else
# Linux 下工具链位置
TOOL_DIR := /opt/jieli/pi32v2/bin
CC    := clang
CXX   := clang
LD    := lto-wrapper
AR    := lto-ar
MKDIR := mkdir -p
RM    := rm -rf
export OBJDUMP := $(TOOL_DIR)/objdump
export OBJCOPY := $(TOOL_DIR)/objcopy
export OBJSIZEDUMP := $(TOOL_DIR)/objsizedump

SYS_LIB_DIR := $(TOOL_DIR)/../lib/r3-large
SYS_INC_DIR := $(TOOL_DIR)/../include
EXT_CFLAGS  := -D__SHELL__ # Linux 下需要这个保证正确处理 download.c
export PATH:=$(TOOL_DIR):$(PATH)

## 后处理脚本
FIXBAT          := touch # Linux下不需要处理 bat 编码问题
POST_SCRIPT     := cpu/br28/tools/download.sh
RUN_POST_SCRIPT := bash $(POST_SCRIPT)
endif

CC  := $(TOOL_DIR)/$(CC)
CXX := $(TOOL_DIR)/$(CXX)
LD  := $(TOOL_DIR)/$(LD)
AR  := $(TOOL_DIR)/$(AR)
# 输出文件设置
OUT_ELF   := cpu/br28/tools/sdk.elf
OBJ_FILE  := $(OUT_ELF).objs.txt
# 编译路径设置
BUILD_DIR := objs

# 编译参数设置
CFLAGS := \
	-target pi32v2 \
	-mcpu=r3 \
	-integrated-as \
	-flto \
	-Wuninitialized \
	-Wno-invalid-noreturn \
	-fno-common \
	-integrated-as \
	-Oz \
	-g \
	-flto \
	-fallow-pointer-null \
	-fprefer-gnu-section \
	-Wno-shift-negative-value \
	-Wframe-larger-than=256 \
	-Wincompatible-pointer-types \
	-Wreturn-type \
	-Wimplicit-function-declaration \
	-mllvm -pi32v2-large-program=true \
	-fms-extensions \
	-fdiscrete-bitfield-abi \


# C++额外的编译参数
CXXFLAGS :=


# 宏定义
DEFINES := \
	-DCONFIG_RELEASE_ENABLE \
	-DCONFIG_EARPHONE_CASE \
	-DCONFIG_CPU_BR28 \
	-DCONFIG_PRINT_IN_MASK \
	-DLWIP_USE_BT \
	-DCONFIG_NEW_BREDR_ENABLE \
	-DCONFIG_NEW_MODEM_ENABLE \
	-DCONFIG_NEW_TWS_FORWARD_ENABLE \
	-DCONFIG_UCOS_ENABLE \
	-DCONFIG_EQ_SUPPORT_ASYNC \
	-DEQ_CORE_V1 \
	-DWTS_DEC_IN_MASK \
	-DCONFIG_AAC_CODEC_FFT_USE_MUTEX \
	-DCONFIG_SIN_TONE_V2 \
	-DCONFIG_DNS_ENABLE \
	-DCONFIG_DMS_MALLOC \
	-DCONFIG_MMU_ENABLE \
	-DCONFIG_SBC_CODEC_HW \
	-DCONFIG_MSBC_CODEC_HW \
	-DCONFIG_AEC_M=128 \
	-DCONFIG_SUPPORT_WIFI_DETECT \
	-DCONFIG_AUDIO_ONCHIP \
	-DCONFIG_MEDIA_NEW_ENABLE \
	-DCONFIG_SUPPORT_EX_TWS_ADJUST \
	-D__GCC_PI32V2__ \
	-DCONFIG_NEW_ECC_ENABLE \
	-DCONFIG_CODE_MOVABLE_ENABLE \
	-DCONFIG_EFFECT_CORE_V2_ENABLE \
	-DCONFIG_OS_AFFINITY_ENABLE=0 \
	-DEVENT_HANDLER_NUM_CONFIG=2 \
	-DEVENT_TOUCH_ENABLE_CONFIG=0 \
	-DEVENT_POOL_SIZE_CONFIG=256 \
	-DCONFIG_EVENT_KEY_MAP_ENABLE=0 \
	-DTIMER_POOL_NUM_CONFIG=15 \
	-DAPP_ASYNC_POOL_NUM_CONFIG=0 \
	-DSDFILE_STORAGE=0 \
	-DVFS_FILE_POOL_NUM_CONFIG=1 \
	-DFS_VERSION=0x020001 \
	-DFATFS_VERSION=0x020101 \
	-DSDFILE_VERSION=0x020000 \
	-DVFS_ENABLE=1 \
	-DVM_MAX_SIZE_CONFIG=16*1024 \
	-DVM_ITEM_MAX_NUM=256 \
	-DCONFIG_EARPHONE_CASE_ENABLE \
	-DCONFIG_NEW_CFG_TOOL_ENABLE \
	-DCONFIG_LITE_AEC_ENABLE=0 \
	-DAUDIO_REC_LITE \
	-DAUDIO_DEC_LITE \
	-DAUDIO_REC_POOL_NUM=1 \
	-DAUDIO_DEC_POOL_NUM=3 \
	-DCONFIG_BTCTRLER_TASK_DEL_ENABLE \
	-DCONFIG_LINK_DISTURB_SCAN_ENABLE=0 \
	-DCONFIG_UPDATA_ENABLE \
	-DCONFIG_OTA_UPDATA_ENABLE \


DEFINES += $(EXT_CFLAGS) # 额外的一些定义

# 头文件搜索路径
INCLUDES := \
	-Iinterface \
	-Iinterface/driver \
	-Iinterface/driver/device \
	-Iinterface/driver/utils \
	-Iinterface/driver/cpu/br28 \
	-Iinterface/driver/cpu/periph \
	-Iinterface/media \
	-Iinterface/system \
	-Iinterface/system/generic \
	-Iinterface/system/device \
	-Iinterface/utils/ui \
	-Iinterface/btctrler \
	-Iinterface/btctrler/port/br28 \
	-Iinterface/update \
	-Iinterface/agreement \
	-Iinterface/btstack/third_party/common \
	-Iinterface/btstack/third_party \
	-Iinterface/utils \
	-Iinterface/utils/generic \
	-Iinterface/utils/device \
	-Iinterface/utils/fs \
	-Iinterface/utils/math/cpu/br28 \
	-Iinterface/net/mbedtls_3_4_0 \
	-Iinterface/btstack/third_party/rcsp \
	-Iinterface/media/media_new \
	-Iinterface/media/media_new/media \
	-Iinterface/media/media_new/media/cpu/br28 \
	-Iinterface/media/media_new/media/cpu/br28/asm \
	-Iinterface/net/lwip_2_2_0/lwip/src/include \
	-Iinterface/net/lwip_2_2_0/lwip/port \
	-Iinterface/net/lwip_2_2_0/lwip/app \
	-Iapps/earphone \
	-Iapps/earphone/include \
	-Iapps/earphone/include/wireless_mic \
	-Iapps/earphone/board/br28 \
	-Iapps/earphone/ui \
	-Iapps/common \
	-Iapps/common/power_manage \
	-Iapps/common/device \
	-Iapps/common/device/sensor \
	-Iapps/common/device/sensor/ntc \
	-Iapps/common/device/key \
	-Iapps/common/device/eartouch \
	-Iapps/common/device/charge	\
	-Iapps/common/device/motor	\
	-Iapps/common/device/led_pt0807	\
	-Iapps/common/include \
	-Iapps/common/ui \
	-Iapps/common/config/include \
	-Iapps/common/dev_manager \
	-Iapps/common/third_party_profile \
	-Iapps/common/third_party_profile/common \
	-Iapps/common/third_party_profile/interface \
	-Iapps/common/third_party_profile/jieli \
	-Iapps/common/third_party_profile/jieli/trans_data_demo \
	-Iapps/common/third_party_profile/jieli/online_db \
	-Icpu \
	-Icpu/br28 \
	-Iaudio \
	-Iaudio/cpu/br28 \
	-Iaudio/cpu/br28/audio_dec \
	-Iaudio/common \
	-Iaudio/effect \
	-Iapps/common/device/sensor/imu_sensor \
	-Iinterface/net/lwip_2_2_0/lwip/src \
	-Iapps/common/third_party_profile/jieli/rcsp \
	-Iapps/common/third_party_profile/jieli/rcsp/adv \
	-Iapps/common/third_party_profile/jieli/rcsp/rcsp_functions \
	-Iapps/common/third_party_profile/jieli/rcsp/server/include \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/device_info \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/device_info/device_status_cmd \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_update \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/external_flash \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/file_transfer \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/sensors \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt \
	-Iapps/common/third_party_profile/jieli/rcsp/server/functions/browser \
	-Iapps/common/third_party_profile/jieli/rcsp/client/rcsp_m_update \
	-Iapps/common/third_party_profile/mma_protocol \
	-Iapps/common/third_party_profile/realme_protocol \
	-Iapps/common/third_party_profile/swift_pair \
	-Iapps/common/third_party_profile/dma_protocol \
	-Iapps/common/third_party_profile/dma_protocol/dma_use_lib \
	-Iapps/common/third_party_profile/gfps_protocol \
	-Iapps/common/third_party_profile/custom_protocol_demo \
	-Iapps/common/third_party_profile/ximalaya_protocol \
	-Iapps/common/third_party_profile/auracast_app \
	-Iapps/common/third_party_profile/rdx_protocol \
	-Iapps/common/cJSON \
	-Iinterface/media/cpu/br28 \
	-Iinterface/utils/ui/ui/cpu/br28 \
	-Iapps/common/le_audio \
	-Iapps/earphone/include/le_audio \
	-Iapps/earphone/include/le_audio/big \
	-Iapps/earphone/include/le_audio/cig \
	-Iinterface/btstack/third_party/le_audio \
	-Iinterface/btstack/third_party/broadcast \
	-Iaudio/le_audio \
	-Iapps/common/third_party_profile/bt_fmy \
	-Iinterface/btstack \
	-Iinterface/net \
	-Iinterface/net/lwip_2_2_0/lwip/src/include/netif \
	-Iinterface/net/lwip_2_2_0/lwip/port/arch \
	-Iinterface/net/lwip_2_2_0/lwip/app/ctp \
	-Iinterface/net/lwip_2_2_0/lwip/app/cdp \
	-Iinterface/net/lwip_2_2_0/lwip/app/dhcp_srv \
	-Iinterface/net/lwip_2_2_0/lwip/app/dtp \
	-Iinterface/net/lwip_2_2_0/lwip/app/sock_api \
	-Iinterface/net/lwip_2_2_0/lwip/app/udtp \
	-Iinterface/net/lwip_2_2_0/lwip/src/include/lwip \
	-Iinterface/net/lwip_2_2_0/lwip/app/mssdp \
	-Iinterface/net/lwip_2_2_0/lwip/app/ddns \
	-Iinterface/net/lte_module \
	-Iapps/common/netapps/iflytek \
	-Ilib/net/lwip_2_2_0/apps/ntp \
	-Iapps/common/net_apps \
	-Ilib/net/web_socket \
	-Iinterface/net/web_socket \
	-Iapps/common/device/usb \
	-Iapps/common/device/usb/device \
	-Iapps/common/device/usb/host \
	-Iaudio/interface/include \
	-Iaudio/framework/include \
	-Iaudio/framework/plugs/source \
	-Iaudio/CVP \
	-Iaudio/test_tools \
	-Iaudio/common/icsd/adjdcc \
	-Iaudio/common/icsd/adt \
	-Iaudio/common/icsd/anc \
	-Iaudio/common/icsd/common \
	-Iaudio/common/icsd/dot \
	-Iaudio/common/icsd/common_v2 \
	-Iaudio/common/icsd/anc_v2 \
	-Iaudio/common/icsd/avc \
	-Iaudio/common/icsd/config \
	-Iaudio/common/icsd/rt_anc \
	-Iaudio/common/icsd/tool \
	-Iaudio/common/icsd/aeq \
	-Iaudio/common/icsd/afq \
	-Iaudio/common/icsd/cmp \
	-Iaudio/common/icsd/demo \
	-Iaudio/common/icsd/wind \
	-Iaudio/common/icsd/vdt \
	-Iaudio/common/icsd/wat \
	-Iaudio/common/icsd/ein \
	-Iaudio/common/icsd/howl \
	-Iaudio/common/power \
	-Iaudio/anc \
	-Iaudio/cpu/br28/audio_dai \
	-Iaudio/smart_voice \
	-Iaudio/effect/spatial_effect \
	-Iaudio/effect/somatosensory \
	-Iinterface/media/aispeech/enc \
	-Iaudio/cpu/br28/audio_vad \
	-Iinterface/media/framework/include \
	-Iinterface/media/cvp \
	-Iapps/common/third_party_profile/Tencent_LL/include \
	-Iapps/common/third_party_profile/Tencent_LL/Tencent_LL_demo \
	-Iapps/common/third_party_profile/Tencent_LL/tencent_ll_demo/jieli_iot_functions \
	-Iinterface/system/math/cpu/br28 \
	-Iaudio/st_opus_enc \
	-I$(SYS_INC_DIR) \


# 需要编译的 .c 文件
c_SRC_FILES := \
	apps/common/app_mode_manager/app_mode_manager.c \
	apps/common/bt_common/bt_fre_offset_storage.c \
	apps/common/cJSON/cJSON.c \
	apps/common/clock_manager/clock_manager.c \
	apps/common/config/app_config.c \
	apps/common/config/bt_name_parse.c \
	apps/common/config/bt_profile_config.c \
	apps/common/config/cfg_tool_cdc.c \
	apps/common/config/cfg_tool_statistics_functions/cfg_tool_statistics.c \
	apps/common/config/ci_transport_uart.c \
	apps/common/config/new_cfg_tool.c \
	apps/common/debug/debug.c \
	apps/common/debug/debug_lite.c \
	apps/common/debug/debug_uart_config.c \
	apps/common/debug/dlog_config.c \
	apps/common/debug/dlog_output_config.c \
	apps/common/dev_manager/dev_manager.c \
	apps/common/dev_manager/dev_reg.c \
	apps/common/dev_manager/dev_update.c \
	apps/common/device/eartouch/eartouch_iic_interface.c \
	apps/common/device/eartouch/eartouch_manage.c \
	apps/common/device/eartouch/hx300x/hx300x_driver.c \
	apps/common/device/eartouch/outside_tch_driver.c \
	apps/common/device/in_ear_detect/in_ear_detect.c \
	apps/common/device/in_ear_detect/in_ear_manage.c \
	apps/common/device/key/adkey.c \
	apps/common/device/key/adkey_rtcvdd.c \
	apps/common/device/key/ctmu_touch_key.c \
	apps/common/device/key/iokey.c \
	apps/common/device/key/irkey.c \
	apps/common/device/key/key_driver.c \
	apps/common/device/key/touch_key.c \
	apps/common/device/key/uart_key.c \
	apps/common/device/sensor/imu_sensor/icm_42670p/icm_42670p.c \
	apps/common/device/sensor/imu_sensor/icm_42670p/inv_imu_apex.c \
	apps/common/device/sensor/imu_sensor/icm_42670p/inv_imu_driver.c \
	apps/common/device/sensor/imu_sensor/icm_42670p/inv_imu_transport.c \
	apps/common/device/sensor/imu_sensor/imuSensor_manage.c \
	apps/common/device/sensor/imu_sensor/lsm6dsl/lsm6dsl.c \
	apps/common/device/sensor/imu_sensor/mpu6887/mpu6887p.c \
	apps/common/device/sensor/imu_sensor/mpu9250/mpu9250.c \
	apps/common/device/sensor/imu_sensor/qmi8658/qmi8658c.c \
	apps/common/device/sensor/imu_sensor/sh3001/sh3001.c \
	apps/common/device/sensor/ir_sensor/ir_manage.c \
	apps/common/device/sensor/ir_sensor/jsa1221.c \
	apps/common/device/sensor/ntc/ntc_det.c \
	apps/common/device/storage_device/norflash/norflash_sfc.c \
	apps/common/device/usb/demo/host/udisk_block_test.c \
	apps/common/device/usb/device/cdc.c \
	apps/common/device/usb/device/custom_hid.c \
	apps/common/device/usb/device/descriptor.c \
	apps/common/device/usb/device/hid.c \
	apps/common/device/usb/device/midi.c \
	apps/common/device/usb/device/msd.c \
	apps/common/device/usb/device/msd_upgrade.c \
	apps/common/device/usb/device/mtp.c \
	apps/common/device/usb/device/printer.c \
	apps/common/device/usb/device/rcsp_hid_inter.c \
	apps/common/device/usb/device/task_pc.c \
	apps/common/device/usb/device/uac1.c \
	apps/common/device/usb/device/uac2.c \
	apps/common/device/usb/device/uac_stream.c \
	apps/common/device/usb/device/usb_device.c \
	apps/common/device/usb/device/usb_pll_trim.c \
	apps/common/device/usb/device/user_setup.c \
	apps/common/device/usb/host/adb.c \
	apps/common/device/usb/host/aoa.c \
	apps/common/device/usb/host/hid.c \
	apps/common/device/usb/host/uac_host.c \
	apps/common/device/usb/host/usb_bulk_transfer.c \
	apps/common/device/usb/host/usb_ctrl_transfer.c \
	apps/common/device/usb/host/usb_host.c \
	apps/common/device/usb/host/usb_storage.c \
	apps/common/device/usb/usb_config.c \
	apps/common/device/usb/usb_epbuf_manager.c \
	apps/common/device/usb/usb_host_config.c \
	apps/common/device/usb/usb_task.c \
	apps/common/device/charge/sk4558.c \
	apps/common/device/motor/motor.c \
	apps/common/device/led_pt0807/led_pt0807.c \
	apps/common/fat_nor/cfg_private.c \
	apps/common/fat_nor/virfat_flash.c \
	apps/common/file_operate/file_manager.c \
	apps/common/le_audio/big.c \
	apps/common/le_audio/cig.c \
	apps/common/le_audio/wireless_trans_manager.c \
	apps/common/music/breakpoint.c \
	apps/common/music/music_decrypt.c \
	apps/common/music/music_id3.c \
	apps/common/music/music_player.c \
	apps/common/net/lwip_2_2_0/port/LwIP.c \
	apps/common/net/lwip_2_2_0/port/bt_ethernetif.c \
	apps/common/net/lwip_2_2_0/port/fixme.c \
	apps/common/net/lwip_2_2_0/port/sys_arch.c \
	apps/common/netapps/iflytek/ifly_api.c \
	apps/common/netapps/iflytek/ifly_auth.c \
	apps/common/netapps/iflytek/ifly_socket.c \
	apps/common/netapps/iflytek/ifly_sparkdesk.c \
	apps/common/netapps/iflytek/ifly_tts.c \
	apps/common/netapps/iflytek/ifly_vad.c \
	apps/common/netapps/iflytek/ifly_vad_demo.c \
	apps/common/temp_trim/dtemp_pll_trim.c \
	apps/common/third_party_profile/Tencent_LL/tencent_ll_demo/jieli_iot_functions/ble_iot_anc_manager.c \
	apps/common/third_party_profile/Tencent_LL/tencent_ll_demo/jieli_iot_functions/ble_iot_msg_manager.c \
	apps/common/third_party_profile/Tencent_LL/tencent_ll_demo/jieli_iot_functions/ble_iot_power_manager.c \
	apps/common/third_party_profile/Tencent_LL/tencent_ll_demo/jieli_iot_functions/ble_iot_tws_manager.c \
	apps/common/third_party_profile/Tencent_LL/tencent_ll_demo/jieli_iot_functions/ble_iot_utils.c \
	apps/common/third_party_profile/Tencent_LL/tencent_ll_demo/ll_demo.c \
	apps/common/third_party_profile/Tencent_LL/tencent_ll_demo/ll_sync_event_handler.c \
	apps/common/third_party_profile/Tencent_LL/tencent_ll_demo/ll_task.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_import.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_llsync_data.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_llsync_device.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_llsync_event.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_llsync_ota.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_service.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_template.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_utils_base64.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_utils_crc.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_utils_hmac.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_utils_log.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_utils_md5.c \
	apps/common/third_party_profile/Tencent_LL/tencent_protocol/ble_qiot_utils_sha1.c \
	apps/common/third_party_profile/ancs_client_demo/le_ancs_client.c \
	apps/common/third_party_profile/auracast_app/auracast_app_ble.c \
	apps/common/third_party_profile/auracast_app/auracast_app_gatt_over_edr.c \
	apps/common/third_party_profile/auracast_app/auracast_app_protocol.c \
	apps/common/third_party_profile/auracast_app/auracast_app_sink_api.c \
	apps/common/third_party_profile/auracast_app/auracast_app_source_api.c \
	apps/common/third_party_profile/bt_fmy/ble_fmy.c \
	apps/common/third_party_profile/bt_fmy/ble_fmy_fmna.c \
	apps/common/third_party_profile/bt_fmy/ble_fmy_modet.c \
	apps/common/third_party_profile/bt_fmy/ble_fmy_ota.c \
	apps/common/third_party_profile/common/3th_profile_api.c \
	apps/common/third_party_profile/common/custom_cfg.c \
	apps/common/third_party_profile/common/mic_rec.c \
	apps/common/third_party_profile/custom_protocol_demo/custom_protocol.c \
	apps/common/third_party_profile/rdx_protocol/rdx_wifi_event.c \
	apps/common/third_party_profile/rdx_protocol/rdx_app.c \
	apps/common/third_party_profile/rdx_protocol/rdx_vm.c \
	apps/common/third_party_profile/rdx_protocol/rdx_dut.c \
	apps/common/third_party_profile/rdx_protocol/rdx_record.c \
	apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c \
	apps/common/third_party_profile/rdx_protocol/rdx_led_ctrl.c \
	apps/common/third_party_profile/rdx_protocol/rdx_spp.c \
	apps/common/third_party_profile/rdx_protocol/rdx_util.c \
	apps/common/third_party_profile/rdx_protocol/rdx_common.c \
	apps/common/third_party_profile/rdx_protocol/rdx_ota.c \
	apps/common/third_party_profile/rdx_protocol/rdx_md5.c \
	apps/common/third_party_profile/rdx_protocol/rdx_rtc.c \
	apps/common/third_party_profile/rdx_protocol/rdx_key.c \
	apps/common/third_party_profile/rdx_protocol/rdx_charge.c \
	apps/common/third_party_profile/rdx_protocol/rdx_spi.c \
	apps/common/third_party_profile/rdx_protocol/rdx_battery.c \
	apps/common/third_party_profile/rdx_protocol/rdx_dip_switch.c \
	apps/common/third_party_profile/dma_protocol/dma_protocol.c \
	apps/common/third_party_profile/dma_protocol/dma_use_lib/dma_le_port.c \
	apps/common/third_party_profile/dma_protocol/dma_use_lib/dma_setting.c \
	apps/common/third_party_profile/dma_protocol/dma_use_lib/dma_spp_port.c \
	apps/common/third_party_profile/dma_protocol/dma_use_lib/platform_template.c \
	apps/common/third_party_profile/gfps_protocol/gfps_protocol.c \
	apps/common/third_party_profile/interface/app_protocol_api.c \
	apps/common/third_party_profile/interface/app_protocol_common.c \
	apps/common/third_party_profile/interface/app_protocol_dma.c \
	apps/common/third_party_profile/interface/app_protocol_gma.c \
	apps/common/third_party_profile/interface/app_protocol_mma.c \
	apps/common/third_party_profile/interface/app_protocol_ota.c \
	apps/common/third_party_profile/interface/app_protocol_tme.c \
	apps/common/third_party_profile/jieli/online_db/online_db_deal.c \
	apps/common/third_party_profile/jieli/online_db/spp_online_db.c \
	apps/common/third_party_profile/jieli/rcsp/adv/ble_rcsp_adv.c \
	apps/common/third_party_profile/jieli/rcsp/ble_rcsp_client.c \
	apps/common/third_party_profile/jieli/rcsp/ble_rcsp_multi_common.c \
	apps/common/third_party_profile/jieli/rcsp/ble_rcsp_server.c \
	apps/common/third_party_profile/jieli/rcsp/client/rcsp_c_cmd_recieve.c \
	apps/common/third_party_profile/jieli/rcsp/client/rcsp_c_cmd_recieve_no_response.c \
	apps/common/third_party_profile/jieli/rcsp/client/rcsp_c_cmd_response.c \
	apps/common/third_party_profile/jieli/rcsp/client/rcsp_m_update/rcsp_update_master.c \
	apps/common/third_party_profile/jieli/rcsp/rcsp_functions/rcsp.c \
	apps/common/third_party_profile/jieli/rcsp/rcsp_functions/rcsp_bt_manage.c \
	apps/common/third_party_profile/jieli/rcsp/rcsp_functions/rcsp_config.c \
	apps/common/third_party_profile/jieli/rcsp/rcsp_functions/rcsp_event.c \
	apps/common/third_party_profile/jieli/rcsp/rcsp_functions/rcsp_manage.c \
	apps/common/third_party_profile/jieli/rcsp/rcsp_functions/rcsp_task.c \
	apps/common/third_party_profile/jieli/rcsp/rcsp_interface.c \
	apps/common/third_party_profile/jieli/rcsp/rcsp_simplified/rcsp_interface_simplified.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/browser/rcsp_browser.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/device_info/device_status_cmd/rcsp_bt_func.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/device_info/device_status_cmd/rcsp_fm_func.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/device_info/device_status_cmd/rcsp_linein_func.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/device_info/device_status_cmd/rcsp_music_func.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/device_info/device_status_cmd/rcsp_pc_func.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/device_info/device_status_cmd/rcsp_rtc_func.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/device_info/device_status_cmd/rcsp_spdif_func.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/device_info/rcsp_device_feature.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/device_info/rcsp_device_status.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/external_flash/rcsp_extra_flash_cmd.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/external_flash/rcsp_extra_flash_opt.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/file_transfer/dev_format.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/file_transfer/file_bluk_trans_prepare.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/file_transfer/file_delete.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/file_transfer/file_simple_transfer.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/file_transfer/file_trans_back.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/file_transfer/file_transfer.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/rcsp_adv_bluetooth.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/rcsp_setting_opt.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/rcsp_setting_sync.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_1t2_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_adaptive_noise_reduction.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_ai_no_pick.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_anc_voice.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_anc_voice_key.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_bt_name_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_hearing_aid_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_key_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_led_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_mic_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_scene_noise_reduction.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_time_stamp_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_voice_enhancement_mode.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_wind_noise_detection.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/adv_work_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_color_led_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_eq_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_high_low_vol_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_karaoke_eq_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_karaoke_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_misc_drc_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_misc_reverbration_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_misc_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_music_info_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_setting_opt/settings/rcsp_vol_setting.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_switch_device.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_update/rcsp_ch_loader_download.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_update/rcsp_update.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/rcsp_update/rcsp_update_tws.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/nfc_data_opt.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sensor_log_notify.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sensors_data_opt.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_func.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_bt_disconn.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_continuous_heart_rate.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_exercise_heart_rate.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_fall_detection.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_personal_info.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_pressure_detection.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_raise_wrist.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_sedentary.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_sensor_opt.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_info_opt/sport_info_sleep_detection.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_air_pressure.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_altitude.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_blood_oxygen.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_exercise_recovery_time.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_exercise_steps.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_heart_rate.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_max_oxygen_uptake.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_pressure_detection.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_sports_information.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_data_opt/sport_data_training_load.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_info_opt.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_info_sync.c \
	apps/common/third_party_profile/jieli/rcsp/server/functions/sensors/sport_info_vm.c \
	apps/common/third_party_profile/jieli/rcsp/server/rcsp_cmd_recieve.c \
	apps/common/third_party_profile/jieli/rcsp/server/rcsp_cmd_recieve_no_respone.c \
	apps/common/third_party_profile/jieli/rcsp/server/rcsp_cmd_respone.c \
	apps/common/third_party_profile/jieli/rcsp/server/rcsp_cmd_user.c \
	apps/common/third_party_profile/jieli/rcsp/server/rcsp_command.c \
	apps/common/third_party_profile/jieli/rcsp/server/rcsp_data_recieve.c \
	apps/common/third_party_profile/jieli/rcsp/server/rcsp_data_recieve_no_respone.c \
	apps/common/third_party_profile/jieli/rcsp/server/rcsp_data_respone.c \
	apps/common/third_party_profile/jieli/trans_data_demo/le_trans_data.c \
	apps/common/third_party_profile/jieli/trans_data_demo/spp_trans_data.c \
	apps/common/third_party_profile/mma_protocol/mma_config.c \
	apps/common/third_party_profile/mma_protocol/mma_platform_api.c \
	apps/common/third_party_profile/mma_protocol/mma_protocol.c \
	apps/common/third_party_profile/multi_protocol_common.c \
	apps/common/third_party_profile/multi_protocol_event.c \
	apps/common/third_party_profile/multi_protocol_main.c \
	apps/common/third_party_profile/realme_protocol/realme_config.c \
	apps/common/third_party_profile/realme_protocol/realme_platform_api.c \
	apps/common/third_party_profile/realme_protocol/realme_protocol.c \
	apps/common/third_party_profile/swift_pair/swift_pair_protocol.c \
	apps/common/third_party_profile/ximalaya_protocol/xmly_ble.c \
	apps/common/third_party_profile/ximalaya_protocol/xmly_config.c \
	apps/common/third_party_profile/ximalaya_protocol/xmly_platform_api.c \
	apps/common/third_party_profile/ximalaya_protocol/xmly_protocol.c \
	apps/common/ui/pwm_led/led_ui_api.c \
	apps/common/ui/pwm_led/led_ui_tws_sync.c \
	apps/common/update/testbox_uart_update.c \
	apps/common/update/testbox_update.c \
	apps/common/update/update.c \
	apps/common/update/update_tws.c \
	apps/common/update/update_tws_new.c \
	apps/earphone/app_main.c \
	apps/earphone/rtc_test.c \
	apps/earphone/audio/jlstream_event_handler.c \
	apps/earphone/audio/scene_switch.c \
	apps/earphone/audio/tone_table.c \
	apps/earphone/audio/vol_sync.c \
	apps/earphone/battery/battery_level.c \
	apps/earphone/battery/charge.c \
	apps/earphone/battery/charge_clibration.c \
	apps/earphone/battery/charge_device_handle.c \
	apps/earphone/battery/charge_store.c \
	apps/earphone/ble/ble_adv.c \
	apps/earphone/ble/bt_ble.c \
	apps/earphone/board/br28/board_config.c \
	apps/earphone/board/instruction_forbidden.c \
	apps/earphone/board/sdk_board_config.c \
	apps/earphone/demo/att_over_edr_demo.c \
	apps/earphone/demo/debug_record_demo.c \
	apps/earphone/demo/pbg_demo.c \
	apps/earphone/demo/read_sn_demo.c \
	apps/earphone/demo/trans_data_demo.c \
	apps/earphone/device_config.c \
	apps/earphone/log_config/app_config.c \
	apps/earphone/log_config/lib_btctrler_config.c \
	apps/earphone/log_config/lib_btstack_config.c \
	apps/earphone/log_config/lib_driver_config.c \
	apps/earphone/log_config/lib_media_config.c \
	apps/earphone/log_config/lib_net_config.c \
	apps/earphone/log_config/lib_system_config.c \
	apps/earphone/log_config/lib_update_config.c \
	apps/earphone/message/adapter/app_msg.c \
	apps/earphone/message/adapter/audio_player.c \
	apps/earphone/message/adapter/battery.c \
	apps/earphone/message/adapter/btstack.c \
	apps/earphone/message/adapter/driver.c \
	apps/earphone/message/adapter/eartouch.c \
	apps/earphone/message/adapter/key.c \
	apps/earphone/message/adapter/tws.c \
	apps/earphone/mode/bt/a2dp_play.c \
	apps/earphone/mode/bt/bt_background.c \
	apps/earphone/mode/bt/bt_call_kws_handler.c \
	apps/earphone/mode/bt/bt_event_func.c \
	apps/earphone/mode/bt/bt_key_func.c \
	apps/earphone/mode/bt/bt_key_msg_table.c \
	apps/earphone/mode/bt/bt_net_event.c \
	apps/earphone/mode/bt/bt_slience_detect.c \
	apps/earphone/mode/bt/bt_tws.c \
	apps/earphone/mode/bt/dual_a2dp_play.c \
	apps/earphone/mode/bt/dual_conn.c \
	apps/earphone/mode/bt/dual_phone_call.c \
	apps/earphone/mode/bt/earphone.c \
	apps/earphone/mode/bt/eartch_event_deal.c \
	apps/earphone/mode/bt/kws_voice_event_deal.c \
	apps/earphone/mode/bt/le_audio/big/app_le_auracast.c \
	apps/earphone/mode/bt/le_audio/big/le_broadcast.c \
	apps/earphone/mode/bt/le_audio/big/le_broadcast_config.c \
	apps/earphone/mode/bt/le_audio/cig/app_le_connected.c \
	apps/earphone/mode/bt/le_audio/cig/le_connected.c \
	apps/earphone/mode/bt/le_audio/cig/le_connected_config.c \
	apps/earphone/mode/bt/le_audio/le_audio_common.c \
	apps/earphone/mode/bt/low_latency.c \
	apps/earphone/mode/bt/phone_call.c \
	apps/earphone/mode/bt/poweroff.c \
	apps/earphone/mode/bt/sniff.c \
	apps/earphone/mode/bt/tone.c \
	apps/earphone/mode/bt/tws_dual_conn.c \
	apps/earphone/mode/bt/tws_dual_share.c \
	apps/earphone/mode/bt/tws_phone_call.c \
	apps/earphone/mode/bt/tws_poweroff.c \
	apps/earphone/mode/common/app_default_msg_handler.c \
	apps/earphone/mode/common/dev_status.c \
	apps/earphone/mode/idle/idle.c \
	apps/earphone/mode/idle/idle_key_msg_table.c \
	apps/earphone/mode/key_tone.c \
	apps/earphone/mode/linein/linein.c \
	apps/earphone/mode/linein/linein_dev.c \
	apps/earphone/mode/linein/linein_key_msg_table.c \
	apps/earphone/mode/mic_effect/mic_effect.c \
	apps/earphone/mode/music/music.c \
	apps/earphone/mode/music/music_app_msg_handler.c \
	apps/earphone/mode/music/music_key_msg_table.c \
	apps/earphone/mode/pc/pc.c \
	apps/earphone/mode/pc/pc_key_msg_table.c \
	apps/earphone/mode/power_on/power_on.c \
	apps/earphone/test/rssi.c \
	apps/earphone/test/tws_role_switch.c \
	apps/earphone/third_part/app_protocol_deal.c \
	apps/earphone/tools/app_ancbox.c \
	apps/earphone/tools/app_anctool.c \
	apps/earphone/tools/app_key_dut.c \
	apps/earphone/tools/app_testbox.c \
	apps/earphone/ui/led/led_config.c \
	apps/earphone/ui/led/led_ui_msg_handler.c \
	apps/earphone/user_cfg.c \
	audio/CVP/audio_aec.c \
	audio/CVP/audio_cvp.c \
	audio/CVP/audio_cvp_3mic.c \
	audio/CVP/audio_cvp_ais_3mic.c \
	audio/CVP/audio_cvp_config.c \
	audio/CVP/audio_cvp_demo.c \
	audio/CVP/audio_cvp_develop.c \
	audio/CVP/audio_cvp_dms.c \
	audio/CVP/audio_cvp_online.c \
	audio/CVP/audio_cvp_ref_task.c \
	audio/CVP/audio_cvp_sync.c \
	audio/anc/audio_anc_common.c \
	audio/anc/audio_anc_common_plug.c \
	audio/anc/audio_anc_debug_tool.c \
	audio/anc/audio_anc_fade_ctr.c \
	audio/anc/audio_anc_mult_scene.c \
	audio/common/amplitude_statistic.c \
	audio/common/audio_build_needed.c \
	audio/common/audio_dvol.c \
	audio/common/audio_effect_verify.c \
	audio/common/audio_event_handler.c \
	audio/common/audio_export_demo.c \
	audio/common/audio_general.c \
	audio/common/audio_iis_lrclk_capture.c \
	audio/common/audio_node_config.c \
	audio/common/audio_noise_gate.c \
	audio/common/audio_ns.c \
	audio/common/audio_plc.c \
	audio/common/audio_utils.c \
	audio/common/audio_volume_mixer.c \
	audio/common/bt_audio_energy_detection.c \
	audio/common/debug/audio_debug.c \
	audio/common/demo/audio_alink_demo.c \
	audio/common/demo/audio_eq_demo.c \
	audio/common/demo/codec_demo/audio_enc_file_demo.c \
	audio/common/demo/codec_demo/audio_file_dec_demo.c \
	audio/common/demo/codec_demo/audio_frame_codec_demo.c \
	audio/common/demo/codec_demo/audio_msbc_hw_codec_demo.c \
	audio/common/demo/codec_demo/audio_msbc_sw_codec_demo.c \
	audio/common/demo/codec_demo/audio_opus_codec_demo.c \
	audio/common/demo/codec_demo/audio_sbc_codec_demo.c \
	audio/common/demo/codec_demo/audio_speex_codec_demo.c \
	audio/common/frame_length_adaptive.c \
	audio/common/icsd/adjdcc/icsd_adjdcc_config.c \
	audio/common/icsd/adt/icsd_adt.c \
	audio/common/icsd/adt/icsd_adt_alg.c \
	audio/common/icsd/adt/icsd_adt_app.c \
	audio/common/icsd/adt/icsd_adt_config.c \
	audio/common/icsd/adt/icsd_adt_demo.c \
	audio/common/icsd/aeq/icsd_aeq.c \
	audio/common/icsd/aeq/icsd_aeq_app.c \
	audio/common/icsd/aeq/icsd_aeq_config.c \
	audio/common/icsd/afq/icsd_afq.c \
	audio/common/icsd/afq/icsd_afq_app.c \
	audio/common/icsd/afq/icsd_afq_config.c \
	audio/common/icsd/anc/icsd_anc_app.c \
	audio/common/icsd/anc/icsd_anc_board.c \
	audio/common/icsd/anc/icsd_anc_data.c \
	audio/common/icsd/anc/icsd_anc_interactive.c \
	audio/common/icsd/anc_v2/icsd_anc_v2.c \
	audio/common/icsd/anc_v2/icsd_anc_v2_app.c \
	audio/common/icsd/anc_v2/icsd_anc_v2_interactive.c \
	audio/common/icsd/avc/icsd_avc_app.c \
	audio/common/icsd/avc/icsd_avc_config.c \
	audio/common/icsd/cmp/icsd_cmp_app.c \
	audio/common/icsd/common/icsd_common.c \
	audio/common/icsd/common_v2/icsd_common_v2.c \
	audio/common/icsd/common_v2/icsd_common_v2_app.c \
	audio/common/icsd/config/icsd_anc_v2_config.c \
	audio/common/icsd/config/icsd_cmp_config.c \
	audio/common/icsd/demo/icsd_demo.c \
	audio/common/icsd/dot/icsd_dot.c \
	audio/common/icsd/dot/icsd_dot_app.c \
	audio/common/icsd/ein/icsd_ein_config.c \
	audio/common/icsd/env_noise_det/icsd_env_noise_det_app.c \
	audio/common/icsd/howl/icsd_howl_app.c \
	audio/common/icsd/howl/icsd_howl_config.c \
	audio/common/icsd/rt_anc/rt_anc.c \
	audio/common/icsd/rt_anc/rt_anc_app.c \
	audio/common/icsd/rt_anc/rt_anc_config.c \
	audio/common/icsd/tool/anc_ext_tool.c \
	audio/common/icsd/tool/anc_ext_tool_file.c \
	audio/common/icsd/vdt/icsd_vdt_app.c \
	audio/common/icsd/vdt/icsd_vdt_config.c \
	audio/common/icsd/wat/icsd_wat_app.c \
	audio/common/icsd/wat/icsd_wat_config.c \
	audio/common/icsd/wind/cvp_wind_app.c \
	audio/common/icsd/wind/icsd_wind_app.c \
	audio/common/icsd/wind/icsd_wind_config.c \
	audio/common/online_debug/aud_data_export.c \
	audio/common/online_debug/aud_mic_dut.c \
	audio/common/online_debug/aud_spatial_effect_dut.c \
	audio/common/online_debug/audio_capture.c \
	audio/common/online_debug/audio_online_debug.c \
	audio/common/pcm_data/sine_pcm.c \
	audio/common/power/mic_power_manager.c \
	audio/common/uartPcmSender.c \
	audio/common/wm8978/iic.c \
	audio/common/wm8978/wm8978.c \
	audio/cpu/br28/audio_accelerator/hw_fft.c \
	audio/cpu/br28/audio_anc.c \
	audio/cpu/br28/audio_config.c \
	audio/cpu/br28/audio_dai/audio_pdm.c \
	audio/cpu/br28/audio_demo/audio_adc_demo.c \
	audio/cpu/br28/audio_demo/audio_dac_demo.c \
	audio/cpu/br28/audio_demo/audio_fft_demo.c \
	audio/cpu/br28/audio_demo/audio_pdm_demo.c \
	audio/cpu/br28/audio_mic_capless.c \
	audio/cpu/br28/audio_setup.c \
	audio/cpu/br28/audio_vad/vad_clock_trim.c \
	audio/cpu/br28/audio_vad/vad_mic.c \
	audio/cpu/br28/icsd_anc_user.c \
	audio/cpu/common.c \
	audio/effect/audio_dc_offset_remove.c \
	audio/effect/audio_pitch_speed_api.c \
	audio/effect/audio_voice_changer_api.c \
	audio/effect/bass_treble.c \
	audio/effect/demo/autotune_demo.c \
	audio/effect/demo/bass_treble_demo.c \
	audio/effect/demo/chorus_demo.c \
	audio/effect/demo/crossover_demo.c \
	audio/effect/demo/drc_advance_demo.c \
	audio/effect/demo/drc_demo.c \
	audio/effect/demo/dynamic_eq_demo.c \
	audio/effect/demo/echo_demo.c \
	audio/effect/demo/energy_detect_demo.c \
	audio/effect/demo/eq_demo.c \
	audio/effect/demo/frequency_shift_howling_demo.c \
	audio/effect/demo/gain_demo.c \
	audio/effect/demo/harmonic_exciter_demo.c \
	audio/effect/demo/multiband_drc_demo.c \
	audio/effect/demo/noisegate_demo.c \
	audio/effect/demo/notch_howling_demo.c \
	audio/effect/demo/pitch_speed_demo.c \
	audio/effect/demo/reverb_advance_demo.c \
	audio/effect/demo/reverb_demo.c \
	audio/effect/demo/spectrum_demo.c \
	audio/effect/demo/stereo_widener_demo.c \
	audio/effect/demo/surround_demo.c \
	audio/effect/demo/virtual_bass_demo.c \
	audio/effect/demo/voice_changer_demo.c \
	audio/effect/effects_adj.c \
	audio/effect/effects_default_param.c \
	audio/effect/effects_dev.c \
	audio/effect/eq_config.c \
	audio/effect/esco_ul_voice_changer.c \
	audio/effect/node_param_update.c \
	audio/effect/scene_update.c \
	audio/effect/somatosensory/audio_somatosensory.c \
	audio/effect/spatial_effect/spatial_effect.c \
	audio/effect/spatial_effect/spatial_effect_imu.c \
	audio/effect/spatial_effect/spatial_effect_tws.c \
	audio/effect/spatial_effect/spatial_effects_process.c \
	audio/effect/spatial_effect/spatial_imu_trim.c \
	audio/effect/spk_eq.c \
	audio/framework/nodes/agc_node.c \
	audio/framework/nodes/ai_tx_node.c \
	audio/framework/nodes/channle_swap_node.c \
	audio/framework/nodes/cvp_3mic_node.c \
	audio/framework/nodes/cvp_develop_node.c \
	audio/framework/nodes/cvp_dms_node.c \
	audio/framework/nodes/cvp_sms_node.c \
	audio/framework/nodes/data_export_node.c \
	audio/framework/nodes/dns_node.c \
	audio/framework/nodes/effect_dev0_node.c \
	audio/framework/nodes/effect_dev1_node.c \
	audio/framework/nodes/effect_dev2_node.c \
	audio/framework/nodes/effect_dev3_node.c \
	audio/framework/nodes/effect_dev4_node.c \
	audio/framework/nodes/esco_tx_node.c \
	audio/framework/nodes/iis_node.c \
	audio/framework/nodes/le_audio_source.c \
	audio/framework/nodes/local_tws_source_node.c \
	audio/framework/nodes/multi_ch_iis_node.c \
	audio/framework/nodes/ns_node.c \
	audio/framework/nodes/pc_mic_node.c \
	audio/framework/nodes/plc_node.c \
	audio/framework/nodes/sink_dev0_node.c \
	audio/framework/nodes/sink_dev1_node.c \
	audio/framework/nodes/sink_dev2_node.c \
	audio/framework/nodes/sink_dev3_node.c \
	audio/framework/nodes/sink_dev4_node.c \
	audio/framework/nodes/spatial_effects_node.c \
	audio/framework/nodes/surround_demo_node.c \
	audio/framework/nodes/uart_node.c \
	audio/framework/nodes/volume_node.c \
	audio/framework/plugs/source/a2dp_file.c \
	audio/framework/plugs/source/a2dp_streamctrl.c \
	audio/framework/plugs/source/adc_file.c \
	audio/framework/plugs/source/esco_file.c \
	audio/framework/plugs/source/iis_file.c \
	audio/framework/plugs/source/le_audio_file.c \
	audio/framework/plugs/source/linein_file.c \
	audio/framework/plugs/source/local_tws_file.c \
	audio/framework/plugs/source/multi_ch_adc_file.c \
	audio/framework/plugs/source/multi_ch_iis_file.c \
	audio/framework/plugs/source/pc_spk_file.c \
	audio/framework/plugs/source/pdm_mic_file.c \
	audio/framework/plugs/source/source_dev0_file.c \
	audio/framework/plugs/source/source_dev1_file.c \
	audio/framework/plugs/source/source_dev2_file.c \
	audio/framework/plugs/source/source_dev3_file.c \
	audio/framework/plugs/source/source_dev4_file.c \
	audio/interface/player/a2dp_player.c \
	audio/interface/player/adda_loop_player.c \
	audio/interface/player/dev_flow_player.c \
	audio/interface/player/esco_player.c \
	audio/interface/player/file_player.c \
	audio/interface/player/key_tone_player.c \
	audio/interface/player/le_audio_player.c \
	audio/interface/player/linein_player.c \
	audio/interface/player/local_tws_player.c \
	audio/interface/player/pc_spk_player.c \
	audio/interface/player/ring_player.c \
	audio/interface/player/tone_player.c \
	audio/interface/player/tws_tone_player.c \
	audio/interface/recoder/ai_voice_recoder.c \
	audio/interface/recoder/dev_flow_recoder.c \
	audio/interface/recoder/esco_recoder.c \
	audio/interface/recoder/file_recorder.c \
	audio/interface/recoder/le_audio_recorder.c \
	audio/interface/recoder/pc_mic_recoder.c \
	audio/jl_kws/jl_kws_algo.c \
	audio/jl_kws/jl_kws_audio.c \
	audio/jl_kws/jl_kws_event.c \
	audio/jl_kws/jl_kws_main.c \
	audio/le_audio/le_audio_stream.c \
	audio/smart_voice/jl_kws_platform.c \
	audio/smart_voice/kws_event.c \
	audio/smart_voice/nn_vad.c \
	audio/smart_voice/smart_voice_config.c \
	audio/smart_voice/smart_voice_core.c \
	audio/smart_voice/user_asr.c \
	audio/smart_voice/voice_mic_data.c \
	audio/test_tools/audio_dut_control.c \
	audio/test_tools/audio_dut_control_old.c \
	audio/test_tools/audio_enc_mpt_cvp_ctr.c \
	audio/test_tools/audio_enc_mpt_self.c \
	audio/test_tools/cvp_tool.c \
	audio/test_tools/mic_dut_process.c \
	cpu/br28/charge/charge.c \
	cpu/br28/charge/charge_config.c \
	cpu/br28/charge/chargestore.c \
	cpu/br28/charge/chargestore_config.c \
	cpu/br28/lua_port_api.c \
	cpu/br28/overlay_code.c \
	cpu/br28/periph/led/pwm_led.c \
	cpu/br28/periph/mcpwm.c \
	cpu/br28/periph/rdec.c \
	cpu/br28/periph/touch/lp_touch_key.c \
	cpu/br28/periph/touch/lpctmu_hw.c \
	cpu/br28/periph/touch/plcnt.c \
	cpu/br28/power/key_wakeup.c \
	cpu/br28/power/power_app.c \
	cpu/br28/power/power_config.c \
	cpu/br28/power/power_port.c \
	cpu/br28/private_iis.c \
	cpu/br28/setup.c \
	cpu/br28/ui_driver/led7/led7_driver.c \
	cpu/br28/ui_driver/ui_common.c \
	cpu/components/iic_api.c \
	cpu/components/iic_soft.c \
	cpu/components/ir_decoder.c \
	cpu/components/ir_encoder.c \
	cpu/components/led_api.c \
	cpu/components/rdec_soft.c \
	cpu/components/touch/lp_touch_key_tool.c \
	cpu/components/two_io_led.c \
	cpu/config/gpio_file_parse.c \
	cpu/config/lib_power_config.c \
	cpu/periph_demo/gptimer_demo.c \
	cpu/periph_demo/iic_master_demo.c \
	cpu/periph_demo/iic_slave_demo.c \
	cpu/periph_demo/ir_encoder_decoder_demo.c \
	cpu/periph_demo/led_api_test.c \
	cpu/periph_demo/ledc_test.c \
	cpu/periph_demo/pwm_led_test.c \
	cpu/periph_demo/rdec_soft_demo.c \
	cpu/periph_demo/sd_test.c \
	cpu/periph_demo/spi_test.c \
	cpu/periph_demo/two_io_led_test.c \
	cpu/periph_demo/uart_test.c \
	audio/interface/recoder/translation_ear_recoder.c \


# 需要编译的 .S 文件
S_SRC_FILES := \
	apps/earphone/sdk_version.z.S \


# 需要编译的 .s 文件
s_SRC_FILES :=


# 需要编译的 .cpp 文件
cpp_SRC_FILES :=


# 需要编译的 .cc 文件
cc_SRC_FILES :=


# 需要编译的 .cxx 文件
cxx_SRC_FILES :=


# 链接参数
LFLAGS := \
	--plugin-opt=-pi32v2-always-use-itblock=false \
	--plugin-opt=-enable-ipra=true \
	--plugin-opt=-pi32v2-merge-max-offset=4096 \
	--plugin-opt=-pi32v2-enable-simd=true \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-global-merge-on-const \
	--plugin-opt=-inline-threshold=5 \
	--plugin-opt=-inline-max-allocated-size=32 \
	--plugin-opt=-inline-normal-into-special-section=true \
	--plugin-opt=-dont-used-symbol-list=malloc,free,sprintf,printf,puts,putchar \
	--plugin-opt=save-temps \
	--plugin-opt=-pi32v2-enable-rep-memop \
	--plugin-opt=-warn-stack-size=256 \
	--sort-common \
	--plugin-opt=-used-symbol-file=apps/earphone/sdk_used_list.used \
	--plugin-opt=-enable-movable-region=true \
	--plugin-opt=-movable-region-section-prefix=.movable.slot. \
	--plugin-opt=-movable-region-stub-section-prefix=.movable.stub. \
	--plugin-opt=-movable-region-prefix=.movable.region. \
	--plugin-opt=-movable-region-stub-prefix=__movable_stub_ \
	--plugin-opt=-movable-region-stub-swi-number=-2 \
	--plugin-opt=-movable-region-map-file-list=apps/earphone/movable/funcname.txt \
	--plugin-opt=-movable-region-section-map-file-list=apps/earphone/movable/section.txt \
	--plugin-opt=-movable-region-exclude-func-file-list=apps/earphone/movable/exclude.txt \
	--plugin-opt=-pi32v2-large-program=true \
	--plugin-opt=-floating-point-may-trap=true \
	--gc-sections \
	--plugin-opt=-pi32v2-csync-before-rti=true \
	--start-group \
	cpu/br28/liba/cpu.a \
	cpu/br28/liba/system.a \
	cpu/br28/liba/btstack.a \
	cpu/br28/liba/bt_hash_enc.a \
	cpu/br28/liba/rcsp_stack.a \
	cpu/br28/liba/ximalaya_lib.a \
	cpu/br28/liba/google_fps.a \
	cpu/br28/liba/fmna_stack.a \
	cpu/br28/liba/lib_fmy_auth_br28.a \
	cpu/br28/liba/swift_pair.a \
	cpu/br28/liba/libdma-protocol.a \
	cpu/br28/liba/libdma-device-ota.a \
	cpu/br28/liba/btctrler.a \
	cpu/br28/liba/aec.a \
	cpu/br28/liba/media.a \
	cpu/br28/liba/libepmotion.a \
	cpu/br28/liba/libAptFilt_pi32v2_OnChip.a \
	cpu/br28/liba/libEchoSuppress_pi32v2_OnChip.a \
	cpu/br28/liba/libNoiseSuppress_pi32v2_OnChip.a \
	cpu/br28/liba/libSplittingFilter_pi32v2_OnChip.a \
	cpu/br28/liba/libDelayEstimate_pi32v2_OnChip.a \
	cpu/br28/liba/libSMS_TDE_pi32v2_OnChip.a \
	cpu/br28/liba/libSMS_TDE_Stereo_pi32v2_OnChip.a \
	cpu/br28/liba/libDualMicSystem_pi32v2_OnChip.a \
	cpu/br28/liba/libDualMicSystem_flexible_pi32v2_OnChip.a \
	cpu/br28/liba/libAdaptiveEchoSuppress_pi32v2_OnChip.a \
	cpu/br28/liba/libOpcore_maskrom_pi32v2_OnChip.a \
	cpu/br28/liba/lib_advaudio_plc.a \
	cpu/br28/liba/lib_esco_audio_plc.a \
	cpu/br28/liba/noisegate.a \
	cpu/br28/liba/lib_resample_cal.a \
	cpu/br28/liba/lib_resample_fast_cal.a \
	cpu/br28/liba/sbc_eng_lib.a \
	cpu/br28/liba/lib_sbc_msbc.a \
	cpu/br28/liba/lib_mp3_dec.a \
	cpu/br28/liba/lib_f2a_dec.a \
	cpu/br28/liba/lib_ogg_opus_dec.a \
	cpu/br28/liba/opus_enc_lib.a \
	cpu/br28/liba/aac_enc_lib.a \
	cpu/br28/liba/libLHDCv4DecodeLib.a \
	cpu/br28/liba/libLHDCv5DecodeLib.a \
	cpu/br28/liba/lib_ldac_dec.a \
	cpu/br28/liba/lib_ldac_enc.a \
	cpu/br28/liba/mp3_decstream_lib.a \
	cpu/br28/liba/lib_mty_dec.a \
	cpu/br28/liba/lib_wav_dec.a \
	cpu/br28/liba/lib_flac_dec.a \
	cpu/br28/liba/lib_m4a_dec.a \
	cpu/br28/liba/lib_ape_dec.a \
	cpu/br28/liba/lib_wma_dec.a \
	cpu/br28/liba/agreement.a \
	cpu/br28/liba/libjlsp.a \
	cpu/br28/liba/libjlsp_kws.a \
	cpu/br28/liba/libkwscommon.a \
	cpu/br28/liba/JL_Phone_Call.a \
	cpu/br28/liba/crossover_coff.a \
	cpu/br28/liba/drc.a \
	cpu/br28/liba/lib_drc_advance.a \
	cpu/br28/liba/lib_drc_detect.a \
	cpu/br28/liba/lib_voiceChanger.a \
	cpu/br28/liba/VirtualBass.a \
	cpu/br28/liba/lib_virtual_bass_classic.a \
	cpu/br28/liba/lib_point360_td.a \
	cpu/br28/liba/SensorCalib.a \
	cpu/br28/liba/SpatialAudio.a \
	cpu/br28/liba/SomatosensoryControl.a \
	cpu/br28/liba/DynamicEQ.a \
	cpu/br28/liba/DynamicEQPro.a \
	cpu/br28/liba/lib_icsd_adt.a \
	cpu/br28/liba/lib_icsd_common.a \
	cpu/br28/liba/lib_icsd_dot.a \
	cpu/br28/liba/lib_icsd_anc_v2.a \
	cpu/br28/liba/lib_icsd_common_v2.a \
	cpu/br28/liba/lib_icsd_aeq.a \
	cpu/br28/liba/lib_icsd_afq.a \
	cpu/br28/liba/lib_icsd_cmp.a \
	cpu/br28/liba/lib_icsd_wind.a \
	cpu/br28/liba/lib_icsd_wat.a \
	cpu/br28/liba/lib_icsd_vdt.a \
	cpu/br28/liba/lib_icsd_ein.a \
	cpu/br28/liba/lib_icsd_avc.a \
	cpu/br28/liba/lib_icsd_howl.a \
	cpu/br28/liba/libjlsp_kws_far_keyword.a \
	cpu/br28/liba/libjlsp_kws_india_english.a \
	cpu/br28/liba/3DSpatial.a \
	cpu/br28/liba/lib_plateReverb_adv.a \
	cpu/br28/liba/lib_reverb_cal.a \
	cpu/br28/liba/lib_AutoWah.a \
	cpu/br28/liba/lib_Chorus.a \
	cpu/br28/liba/lib_FrequencyShift.a \
	cpu/br28/liba/howling.a \
	cpu/br28/liba/lib_HarmonicExciter.a \
	cpu/br28/liba/libllns.a \
	cpu/br28/liba/libDualMicSystem_Hybrid.a \
	cpu/br28/liba/libDualMicSystem_Awn.a \
	cpu/br28/liba/lib_ThreeD_effect.a \
	cpu/br28/liba/lib_SurroundEffect.a \
	cpu/br28/liba/lib_StereoWidener.a \
	cpu/br28/liba/lib_PcmDelay.a \
	cpu/br28/liba/crossover_coff.a  \
	cpu/br28/liba/lib_limiter.a \
	cpu/br28/liba/lib_iir_filter.a \
	cpu/br28/liba/spatial_adv.a \
	cpu/br28/liba/le_audio_profile.a \
	cpu/br28/liba/lib_jla_codec.a \
	cpu/br28/liba/lib_lc3_codec.a \
	cpu/br28/liba/lib_jl_codec_common.a \
	cpu/br28/liba/lib_noisegate_pro.a \
	cpu/br28/liba/lib_eq_design_coeff.a \
	cpu/br28/liba/libSAS_pi32v2_OnChip.a \
	cpu/br28/liba/lib_phaser.a \
	cpu/br28/liba/lib_flanger.a \
	cpu/br28/liba/lib_chorus_adv.a \
	cpu/br28/liba/lib_pingpong_echo.a \
	cpu/br28/liba/libStereoSpatialWider_pi32v2_OnChip.a \
	cpu/br28/liba/libdistortion.a \
	cpu/br28/liba/lib_frequency_compressor.a \
	cpu/br28/liba/lib_gain_mix.a \
	cpu/br28/liba/libFFT_pi32v2_OnChip.a \
	cpu/br28/liba/lib_wtg_dec.a \
	cpu/br28/liba/bfilterfun_lib.a \
	cpu/br28/liba/lib_wtgv2_rom_dec.a \
	cpu/br28/liba/lib_bt_aac_dec.a \
	cpu/br28/liba/crypto_toolbox_Osize.a \
	cpu/br28/liba/lib_dns.a \
	cpu/br28/liba/earphone/release/libmbedtls_3_4_0.a \
	cpu/br28/liba/earphone/release/lwip_2_2_0.a \
	cpu/br28/liba/update.a \
	cpu/br28/liba/cbuf.a \
	cpu/br28/liba/lbuf.a \
	cpu/br28/liba/printf.a \
	cpu/br28/liba/device.a \
	cpu/br28/liba/fs.a \
	cpu/br28/liba/ascii.a \
	cpu/br28/liba/cfg_tool.a \
	cpu/br28/liba/vm.a \
	cpu/br28/liba/debug_record.a \
	cpu/br28/liba/lzma_dec.a \
	cpu/br28/liba/chargestore.a \
	cpu/br28/liba/printf.a  \
	cpu/br28/liba/cbuf.a  \
	cpu/br28/liba/vm.a  \
	cpu/br28/liba/fs.a  \
	cpu/br28/liba/cfg_tool.a  \
	audio/st_opus_enc/lib_opus_stenc.a \
	cpu/br28/liba/lwip_2_2_0.a \
	cpu/br28/liba/earphone/release/websocket.a \
	cpu/br28/liba/earphone/release/libmbedtls_3_4_0.a  \
	apps/common/third_party_profile/rdx_protocol/librdxApp.a \
	--end-group \
	-Tcpu/br28/sdk.ld \
	-M=cpu/br28/tools/sdk.map \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-mattr=+fprev1 \


LIBPATHS := \
	-L$(SYS_LIB_DIR) \


LIBS := \
	$(SYS_LIB_DIR)/libm.a \
	$(SYS_LIB_DIR)/libc.a \
	$(SYS_LIB_DIR)/libm.a \
	$(SYS_LIB_DIR)/libcompiler-rt.a \



c_OBJS    := $(c_SRC_FILES:%.c=%.c.o)
S_OBJS    := $(S_SRC_FILES:%.S=%.S.o)
s_OBJS    := $(s_SRC_FILES:%.s=%.s.o)
cpp_OBJS  := $(cpp_SRC_FILES:%.cpp=%.cpp.o)
cxx_OBJS  := $(cxx_SRC_FILES:%.cxx=%.cxx.o)
cc_OBJS   := $(cc_SRC_FILES:%.cc=%.cc.o)

OBJS      := $(c_OBJS) $(S_OBJS) $(s_OBJS) $(cpp_OBJS) $(cxx_OBJS) $(cc_OBJS)
DEP_FILES := $(OBJS:%.o=%.d)


OBJS      := $(addprefix $(BUILD_DIR)/, $(OBJS))
DEP_FILES := $(addprefix $(BUILD_DIR)/, $(DEP_FILES))


VERBOSE ?= 0
ifeq ($(VERBOSE), 1)
QUITE :=
else
QUITE := @
endif

# 一些旧的 make 不支持 file 函数，需要 make 的时候指定 LINK_AT=0 make
LINK_AT ?= 1

# 表示下面的不是一个文件的名字，无论是否存在 all, clean, pre_build 这样的文件
# 还是要执行命令
# see: https://www.gnu.org/software/make/manual/html_node/Phony-Targets.html
.PHONY: all clean pre_build

# 不要使用 make 预设置的规则
# see: https://www.gnu.org/software/make/manual/html_node/Suffix-Rules.html
.SUFFIXES:

all: pre_build $(OUT_ELF)
	$(info +POST-BUILD)
	$(QUITE) $(RUN_POST_SCRIPT) sdk

pre_build:
	$(info +PRE-BUILD)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P apps/earphone/sdk_used_list.c -o apps/earphone/sdk_used_list.used
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P apps/earphone/movable/section.c -o apps/earphone/movable/section.txt
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P cpu/br28/sdk_ld.c -o cpu/br28/sdk.ld
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P cpu/br28/tools/download.c -o $(POST_SCRIPT)
	$(QUITE) $(FIXBAT) $(POST_SCRIPT)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P cpu/br28/tools/isd_config_rule.c -o cpu/br28/tools/isd_config.ini

clean:
	$(QUITE) $(RM) $(OUT_ELF)
	$(QUITE) $(RM) $(BUILD_DIR)



ifeq ($(LINK_AT), 1)
$(OUT_ELF): $(OBJS)
	$(info +LINK $@)
	$(shell $(MKDIR) $(@D))
	$(file >$(OBJ_FILE), $(OBJS))
	$(QUITE) $(LD) -o $(OUT_ELF) @$(OBJ_FILE) $(LFLAGS) $(LIBPATHS) $(LIBS)
else
$(OUT_ELF): $(OBJS)
	$(info +LINK $@)
	$(shell $(MKDIR) $(@D))
	$(QUITE) $(LD) -o $(OUT_ELF) $(OBJS) $(LFLAGS) $(LIBPATHS) $(LIBS)
endif


$(BUILD_DIR)/%.c.o : %.c
	$(info +CC $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.S.o : %.S
	$(info +AS $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.s.o : %.s
	$(info +AS $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.cpp.o : %.cpp
	$(info +CXX $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.cxx.o : %.cxx
	$(info +CXX $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.cc.o : %.cc
	$(info +CXX $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

-include $(DEP_FILES)
