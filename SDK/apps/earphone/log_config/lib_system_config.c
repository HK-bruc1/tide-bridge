/*********************************************************************************************
    *   Filename        : lib_system_config.c

    *   Description     : Optimized Code & RAM (编译优化配置)

    *   Author          : Bingquan

    *   Email           : caibingquan@zh-jieli.com

    *   Last modifiled  : 2019-03-18 15:22

    *   Copyright:(c)JIELI  2011-2019  @ , All Rights Reserved.
*********************************************************************************************/

#include "app_config.h"
#include "system/includes.h"


///打印是否时间打印信息
const int config_printf_time         = 1;

///异常中断，asser打印开启
#if CONFIG_DEBUG_ENABLE || CONFIG_DEBUG_LITE_ENABLE
const int config_asser          = TCFG_EXCEPTION_LOG_ENABLE;  // non 0:使能异常打印; BIT(1):使能当前CPU打印另一个CPU的寄存器信息; BIT(2):使能栈分析回溯函数调用
const int config_exception_reset_enable = TCFG_EXCEPTION_RESET_ENABLE;
const int CONFIG_LOG_OUTPUT_ENABLE = 1;
#else
const int config_asser         = 0;
const int config_exception_reset_enable = 1;
const int CONFIG_LOG_OUTPUT_ENABLE = 0;
#endif

#if CONFIG_DEBUG_LITE_ENABLE
u8 config_debug_lite_en = 1;								// 控制库的轻量级打印
#endif

//================================================//
//                 异常信息记录使能               //
//================================================//
//注意: 当config_asser变量为0时才有效.
#if	(defined TCFG_CONFIG_DEBUG_RECORD_ENABLE && TCFG_CONFIG_DEBUG_RECORD_ENABLE)
const int config_debug_exception_record               = 1; 	 //异常记录功能总开关
const int config_debug_exception_record_dump_info     = 1; 	 //小机上电输出异常信息使能
const int config_debug_exception_record_p11           = 1; 	 //P11异常信息使能
const int config_debug_exception_record_stack         = 1; 	 //堆栈异常信息使能
const int config_debug_exception_record_ret_instrcion = 1; 	 //指令数据异常信息使能
#else /* #if	(define CONFIG_DEBUG_RECORD_ENABLE && CONFIG_DEBUG_RECORD_ENABLE) */
const int config_debug_exception_record               = 0;   //异常记录功能总开关
const int config_debug_exception_record_dump_info     = 0;   //小机上电输出异常信息使能
const int config_debug_exception_record_p11           = 0;   //P11异常信息使能
const int config_debug_exception_record_stack         = 0;   //堆栈异常信息使能
const int config_debug_exception_record_ret_instrcion = 0;   //指令数据异常信息使能
#endif /* #if (define CONFIG_DEBUG_RECORD_ENABLE && CONFIG_DEBUG_RECORD_ENABLE) */

//================================================//
//当hsb的频率小于等于48MHz时，在否自动关闭所有pll //
//================================================//
const int clock_tree_auto_disable_all_pll = 0;

//================================================//
//                  SDFILE 精简使能               //
//================================================//
const int SDFILE_VFS_REDUCE_ENABLE = 1;

//================================================//
//                  dev使用异步读使能             //
//================================================//
#ifdef TCFG_DEVICE_BULK_READ_ASYNC_ENABLE
const int device_bulk_read_async_enable = 1;
#else
const int device_bulk_read_async_enable = 0;
#endif

//================================================//
//                  UI 							  //
//================================================//
//const int ENABLE_LUA_VIRTUAL_MACHINE = 0;

//================================================//
//          不可屏蔽中断使能配置(UNMASK_IRQ)      //
//================================================//
const int CONFIG_CPU_UNMASK_IRQ_ENABLE = 1;


//================================================//
//          heap内存记录功能                      //
//          使能建议设置为256（表示最大记录256项）//
//          通过void mem_unfree_dump()输出        //
//================================================//
const u32 CONFIG_HEAP_MEMORY_TRACE = 0;

//================================================//
//                  FS功能控制 					  //
//================================================//
const int FATFS_WRITE = 1;//0; // 控制fatfs写功能开关。  //dons++ 20250711
const int FILT_0SIZE_ENABLE = 1; //是否过滤0大小文件
const int FATFS_LONG_NAME_ENABLE = 1; //是否支持长文件名
const int FATFS_RENAME_ENABLE = 1; //是否支持重命名
const int FATFS_FGET_PATH_ENABLE = 1; //是否支持获取路径
const int FATFS_SAVE_FAT_TABLE_ENABLE = 1; //是否支持seek加速
const int FATFS_SUPPORT_OVERSECTOR_RW = 0; //是否支持超过一个sector向设备拿数
const int FATFS_TIMESORT_TURN_ENABLE = 1; //按时排序翻转，由默认从小到大变成从大到小
const int FATFS_TIMESORT_NUM = 128; //按时间排序,记录文件数量, 每个占用14 byte

const int FILE_TIME_HIDDEN_ENABLE = 0; //创建文件是否隐藏时间
const int FILE_TIME_USER_DEFINE_ENABLE = 1;//用户自定义时间，每次创建文件前设置，如果置0 需要确定芯片是否有RTC功能。
const int FATFS_SUPPORT_WRITE_SAVE_MEANTIME = 0; //每次写同步目录项使能，会降低连续写速度。
const int FATFS_SUPPORT_WRITE_CUTOFF = 1; //支持fseek截断文件。 打开后fseek后指针位置会决定文件大小
const int FATFS_RW_MAX_CACHE = 64 * 1024; //设置读写申请的最大cache大小 .note: 小于512会被默认不生效

const int FATFS_GET_SPACE_USE_RAM = 0;//32 * 1024;  //获取剩余容量使用大Buf缓存,加快速度, 必须512倍数

const int FATFS_FORMAT_USE_RAM = 0; //32 * 1024;  //格式化功能使用大Buf缓存,加快速度, 必须512倍数

const int FATFS_DEBUG_FAT_TABLE_DIR_ENTRY = 0; //设置debugfat表和目录项写数据

const int VIRFAT_FLASH_ENABLE = 0; //精简jifat代码,不使用。

//================================================//
//phy_malloc碎片整理使能:            			  //
//配置为0: phy_malloc申请不到不整理碎片           //
//配置为1: phy_malloc申请不到会整理碎片           //
//配置为2: phy_malloc申请总会启动一次碎片整理     //
//================================================//
const int PHYSIC_MALLOC_DEFRAG_ENABLE = 1;

//================================================//
//低功耗流程添加内存碎片整理使能:    			  //
//配置为0: 低功耗流程不整理碎片                   //
//配置为1: 低功耗流程会整理碎片                   //
//================================================//
const int MALLOC_MEMORY_DEFRAG_ENABLE = 1;

// 空闲RAM进入SD模式使能    			  //
const int IDLE_RAM_ENTER_SD_MODE_ENABLE = 0;

// 是否使能 dlog 功能
#ifdef TCFG_DEBUG_DLOG_ENABLE
const int config_dlog_enable = TCFG_DEBUG_DLOG_ENABLE;
const int config_dlog_reset_erase_enable = TCFG_DEBUG_DLOG_RESET_ERASE;
const int config_dlog_auto_flush_timeout = TCFG_DEBUG_DLOG_AUTO_FLUSH_TIMEOUT;
#else
const int config_dlog_enable = 0;
const int config_dlog_reset_erase_enable = 0;
const int config_dlog_auto_flush_timeout = 0;
#endif

//查找关中断时间过久函数功能
//用于开启查找中断时间过久的函数功能,打印函数的rets和trance:"irq disable overlimit:"
#if TCFG_IRQ_TIME_DEBUG_ENABLE
const int config_irq_time_debug_enable = TCFG_IRQ_TIME_DEBUG_ENABLE;
const int config_irq_time_debug_time = 10000;  //查找中断时间超过10000us的函数
#else
const int config_irq_time_debug_enable = 0;
const int config_irq_time_debug_time = 0;
#endif

/**
 * @brief Log (Verbose/Info/Debug/Warn/Error)
 */
/*-----------------------------------------------------------*/
const char log_tag_const_v_SYS_TMR  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_SYS_TMR  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_SYS_TMR  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_SYS_TMR  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_SYS_TMR  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_JLFS  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_JLFS  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_JLFS  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_JLFS  = CONFIG_DEBUG_LIB(FALSE);  //dons++
const char log_tag_const_e_JLFS  = CONFIG_DEBUG_LIB(FALSE);  //dons++

//FreeRTOS
const char log_tag_const_v_PORT  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_PORT  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_PORT  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_PORT  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_PORT  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_KTASK  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_KTASK  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_KTASK  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_KTASK  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_KTASK  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_uECC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_uECC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_uECC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_uECC  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_uECC  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_HEAP_MEM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_HEAP_MEM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_HEAP_MEM  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_w_HEAP_MEM  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_HEAP_MEM  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_V_MEM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_V_MEM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_V_MEM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_V_MEM  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_V_MEM  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_P_MEM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_P_MEM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_P_MEM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_P_MEM  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_P_MEM  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_P_MEM_C = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_P_MEM_C = CONFIG_DEBUG_LIB(FALSE);    //dons++
const char log_tag_const_d_P_MEM_C = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_P_MEM_C = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_P_MEM_C = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_DEBUG_RECORD = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_DEBUG_RECORD = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_d_DEBUG_RECORD = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_DEBUG_RECORD = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_DEBUG_RECORD = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_DLOG  = CONFIG_DEBUG_LIB(0);
const char log_tag_const_i_DLOG  = CONFIG_DEBUG_LIB(0);
const char log_tag_const_d_DLOG  = CONFIG_DEBUG_LIB(1);
const char log_tag_const_w_DLOG  = CONFIG_DEBUG_LIB(0);
const char log_tag_const_e_DLOG  = CONFIG_DEBUG_LIB(1);

