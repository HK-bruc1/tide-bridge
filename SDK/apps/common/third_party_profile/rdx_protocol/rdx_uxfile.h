/*=====================================================================================
 HEADER NAME: .c
 MODULE NAME: application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-12-30 10:09:05
 LastEditors: sheng.dong
 LastEditTime: 2024-12-30 12:10:56
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_uxfile.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_UXFILE_H__
#define __RDX_UXFILE_H__

#include <stdbool.h>

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define RECORD_FILE_NAME_SIZE						(64)

#define MEM_FORMAT_RESULT_OK						(0x01)
#define MEM_FORMAT_RESULT_FAIL						(0x10)

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
typedef int (*uxfile_rd_callback)(u8* data_ptr, u32 data_len);

typedef void (*uxfile_format_cb)(u8 result);

enum {
    UXFILE_MSG_GEN = 0xE0,
	UXFILE_MSG_SAVE,
	UXFILE_MSG_WR_TEST,
	UXFILE_MSG_RAW_WR,
	UXFILE_MSG_MIC_REC_CTRL,
	UXFILE_MSG_DEV_FORMAT,
	UXFILE_MSG_DEV_MEM,
	UXFILE_MSG_DELETE,      // 异步删除消息
	UXFILE_MSG_SYNC,        // 开机同步消息
};

typedef struct {
	u8* data;
	u32 data_len;
	u32 left_len;
	u32 sent_size;
	u32 divpack;
	u32 cur_pack_num;
	u32 orig_pack_num;
	bool loading;
} uxfile_datfile_info_t;

typedef struct {
	u32 sn;
	char filename[RECORD_FILE_NAME_SIZE];
	u8 record_scene;
	u32 divpack;
	u32 remain_pack_len;
	u32 left_len;
	u32 data_len;
	u8* data;
	u32 start_time;
	u32 end_time;
	u32 crc32;
	u32 frame_size;
	void* file_handle;      // [优化] 持久化文件句柄，避免每次读取都 fopen/fclose
	u32 file_cur_offset;    // [优化] 当前文件读取偏移，用于顺序读取优化
} uxfile_data_t;

typedef struct {
	u32 cur_num;
	u32 total_size;
	u32 total_count;
} uxfile_filelist_t;

typedef struct{
    int ack;
    int file_num;
    u8 is_first_pack;
    int pack_num;
    int orig_pack_num;
    int sent_size;
    int auto_del;
    int file_offset;
    u32 total_pack;
    u32 chunk;
    u32 block_cnt;
    u8 file_send_busy;
    u8 loop;
    u8 interrupt;
    u8 send_stop;
	u8 ble_upload_cancel;
}ReqFileInfo;

/******************************************************************************
* Function Section
******************************************************************************/ 
void rdx_uxfile_init(void);
int rdx_uxfile_dat_init(void);
int rdx_uxfile_sync_files_with_dat(void);
void rdx_uxfile_print_file_list(void);
void rdx_uxfile_start_timer(void);
void rdx_uxfile_stop_timer(void);
void rdx_uxfile_mssg_generate(bool is_new);
void rdx_uxfile_mssg_1_generate(u8 scene);
void rdx_uxfile_mssg_1_save(void);
int rdx_uxfile_raw_write(u8* data_ptr,u32 data_len, u8 scene);
void rdx_uxfile_mssg_mic_rec_ctrl(int state);
u32 rdx_uxfile_get_all_size(void);
u16 rdx_uxfile_get_dat_cout(void);
u32 rdx_uxfile_get_the_largest_fileSn(void);
uxfile_datfile_info_t* rdx_uxfile_get_datFileInfo(void);
void rdx_uxfile_datFileInfo_sendBuf_free(void);
uxfile_data_t* rdx_uxfile_get_file_data_by_sn(u32 fnum, u8 type, int f_offset);
int rdx_uxfile_raw_read(u32 data_len, u32 off_set, u8* data_ptr);
void rdx_uxfile_close_read_file_handle(void);  // [优化] 关闭持久化文件句柄
void rdx_uxfile_recordFileData_sendBuf_free(void);
void rdx_uxfile_recordFileData_send_finish(ReqFileInfo * rf_info);
void rdx_uxfile_txt_write_test(u8* d, u32 len);
void rdx_uxfile_device_sd_mem_check(void);
void rdx_uxfile_device_sd_format(uxfile_format_cb cb);
int rdx_uxfile_recordFile_delete_handle(int fnum, char* fname);

// [优化] DAT 缓存管理（WiFi传输前调用释放内存）
void rdx_uxfile_free_dat_cache(void);
void rdx_uxfile_invalidate_dat_cache(void);
u32 rdx_uxfile_get_wifi_pack_size(void);
void rdx_uxfile_read_buffer_free(void);
int rdx_uxfile_flush_cache(void);  // 【对齐CC】返回 int，空闲时保存 dirty 缓存到文件

// 【对齐CC】同步状态管理
bool rdx_uxfile_sync_is_in_progress(void);      // 检查同步是否正在进行
void rdx_uxfile_sync_request_pause(void);        // 请求暂停同步
bool rdx_uxfile_sync_wait_pause(int timeout_ms); // 等待同步暂停（带超时）
void rdx_uxfile_sync_resume(void);               // 恢复同步

// 【对齐CC】pending.dat 机制（录音中死机恢复）
void rdx_uxfile_write_pending_marker(void);

// DAT 扫描状态检查
u8 rdx_uxfile_is_sync_in_progress(void);  // 检查 DAT 扫描是否正在进行

// 分时扫描控制接口
void rdx_uxfile_scan_pause(void);         // 暂停异步扫描（录音时调用）
void rdx_uxfile_scan_resume(void);        // 恢复异步扫描（录音结束后调用）
u8 rdx_uxfile_is_scan_active(void);       // 检查异步扫描是否激活

// 关电保护用的状态查询
u8 rdx_uxfile_is_datFileInfo_loading(void); // listreq 分包传输中
u8 rdx_uxfile_is_formatting(void);          // SD 卡正在格式化

#endif/*__RDX_UXFILE_H__*/
