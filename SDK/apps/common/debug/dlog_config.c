#include "typedef.h"
#include "app_config.h"
#include "fs/sdfile.h"
#include "fs/resfile.h"
#include "boot.h"
#include "asm/sfc_norflash_api.h"
#include "norflash.h"
#include "system/includes.h"

__attribute__((weak))
int dlog_output_direct(void *buf, u16 len)
{
    return 0;
}

#if 0
void dlog_print_test1(void *priv)
{
    static int cnt[3] = {0};
    char time[15] = {0};
    int index = (int)priv;

    extern int log_print_time_to_buf(char *time);
    log_print_time_to_buf(time);

    /* printf("%stask:%s, cnt:%d\n", time, os_current_task(), cnt[index]); */
    dlog_printf("%stask:%s, cnt:%d\n", time, os_current_task(), cnt[index]);
    /* dlog_printf("%stask:%s, %s, %s, %s, %s, cnt:%d\n", time, os_current_task(), __func__, __FILE__, __FILE__, __FILE__, cnt[index]); */
    /* printf("%stask:%s, %s, %s, %s, %s, cnt:%d\n", time, os_current_task(), __func__, __FILE__, __FILE__, __FILE__, cnt[index]); */
    cnt[index]++;
}

void dlog_print_in_bttask()
{
    printf("%s, %d", __FUNCTION__, __LINE__);
    sys_timer_add((void *)1, dlog_print_test1, 188);
}

void dlog_print_in_btctrler()
{
    printf("%s, %d", __FUNCTION__, __LINE__);
    sys_timer_add((void *)2, dlog_print_test1, 115);
}

void dlog_print_test(void *priv)
{
    extern void dlog_uart_init();
    dlog_uart_init();
    sys_timer_add((void *)0, dlog_print_test1, 200);
#if 1
    int msg[] = {(int)dlog_print_in_bttask, 0};
    os_taskq_post_type("btstack", Q_CALLBACK, ARRAY_SIZE(msg), msg);
    int msg2[] = {(int)dlog_print_in_btctrler, 0};
    os_taskq_post_type("btctrler", Q_CALLBACK, ARRAY_SIZE(msg2), msg2);
#endif
}
#endif

#if TCFG_DEBUG_DLOG_FLASH_SEL // 外置flash获取其它自定义的区域

extern int _norflash_init(const char *name, struct norflash_dev_platform_data *pdata);
extern int _norflash_open(void *arg);
extern int _norflash_read(u32 addr, u8 *buf, u32 len, u8 cache);
extern int _norflash_write(u32 addr, void *buf, u32 len, u8 cache);
extern int _norflash_eraser(u8 eraser, u32 addr);
extern int _norflash_close(void);

u8 dlog_use_ex_flash = 0;

//重写弱函数
#ifdef CONFIG_CPU_BR36
const struct spi_platform_data spix_p_data[HW_SPI_MAX_NUM] = {
    {
        //spi0
    },
    {
        //spi1
        .port = {
            IO_PORTA_07, //clk any io
            IO_PORTC_00, //do any io
            IO_PORTB_05, //di any io
            0xff, //d2 any io
            0xff, //d3 any io
            0xff, //cs any io(主机不操作cs)
        },
        .role = 0,//SPI_ROLE_MASTER,
        .clk  = 1000000,
        .mode = 0,//SPI_MODE_BIDIR_1BIT,//SPI_MODE_UNIDIR_2BIT,
        .bit_mode = 0, //SPI_FIRST_BIT_MSB
        .cpol = 0,//clk level in idle state:0:low,  1:high
        .cpha = 0,//sampling edge:0:first,  1:second
        .ie_en = 0, //ie enbale:0:disable,  1:enable
        .irq_priority = 3,
        .spi_isr_callback = NULL,  //spi isr callback
    },
    {
        //spi2
    },
};

static const struct norflash_dev_platform_data spi_flash_platform_data = {
    .spi_hw_num     = 1,
    .spi_cs_port    = IO_PORTA_04,
    .spi_read_width = 0,
    .start_addr     = 0,
    .size           = 512 * 1024,
};
#endif
#ifdef CONFIG_CPU_BR52
const struct spi_platform_data spix_p_data[HW_SPI_MAX_NUM] = {
    {
        //spi0
    },
    {
        //spi1
        .port = {
            IO_PORTC_06, //clk any io
            IO_PORTC_07, //do any io
            IO_PORTC_00, //di any io
            0xff, //d2 any io
            0xff, //d3 any io
            0xff, //cs any io(主机不操作cs)
        },
        .role = 0,//SPI_ROLE_MASTER,
        .clk  = 1000000,
        .mode = 0,//SPI_MODE_BIDIR_1BIT,//SPI_MODE_UNIDIR_2BIT,
        .bit_mode = 0, //SPI_FIRST_BIT_MSB
        .cpol = 0,//clk level in idle state:0:low,  1:high
        .cpha = 0,//sampling edge:0:first,  1:second
        .ie_en = 0, //ie enbale:0:disable,  1:enable
        .irq_priority = 3,
        .spi_isr_callback = NULL,  //spi isr callback
    },
    {
        //spi2
    },
};

static const struct norflash_dev_platform_data spi_flash_platform_data = {
    .spi_hw_num     = 1,
    .spi_cs_port    = IO_PORTC_05,
    .spi_read_width = 0,
    .start_addr     = 0,
    .size           = 512 * 1024,
};
#endif

/*----------------------------------------------------------------------------*/
/**@brief 获取保存dlog数据的flash起始地址和大小
  @param  addr: 返回起始地址
  @param  len: 返回长度
  @param  offset: 数据需要写入的flash地址
  @return 等于0表示成功; 小于0表示失败
  @note   起始地址和长度必需 4K 对齐
 */
/*----------------------------------------------------------------------------*/
static int dlog_get_ex_flash_zone(u32 *addr, u32 *len)
{
    // 需要实现
    if (addr) {
        *addr = spi_flash_platform_data.start_addr;
    }
    if (len) {
        *len = spi_flash_platform_data.size;
    }

    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief 将flash的指定扇区擦除
  @param  erase_sector: 要擦除的扇区偏移(偏移是相对于保存dlog数据的flash区域起始地址)
  @param  sector_num: 要擦除的扇区个数
  @param  offset: 数据需要写入的flash地址
  @return 等于0表示成功; 小于0表示失败
  @note
 */
/*----------------------------------------------------------------------------*/
static int dlog_ex_flash_zone_erase(u16 erase_sector, u16 sector_num)
{
    // 需要实现
    /* printf("%s, %d, %d", __func__, erase_sector, sector_num); */

    if (dlog_use_ex_flash) {
        for (int i = 0; i < sector_num; i++) {
            _norflash_eraser(FLASH_SECTOR_ERASER, (erase_sector + i) * LOG_BASE_UNIT_SIZE);
        }
    }
    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief 将dlog数据写入flash
  @param  buf: 要写入的数据
  @param  len: 要写入的数据长度
  @param  offset: 数据需要写入的flash地址
  @return 返回写入的长度,返回值小于0表示写入失败
  @note
 */
/*----------------------------------------------------------------------------*/
static int dlog_ex_flash_write(void *buf, u16 len, u32 offset)
{
    // 需要实现
    if (!len) {
        return 0;
    }

    if (dlog_use_ex_flash) {
        _norflash_write(offset, buf, len, 0);
    }

    /* printf("%s, %d", __func__, len); */
    /* put_buf(buf, len); */
    return len;
}

/*----------------------------------------------------------------------------*/
/**@brief 从flash中读dlog数据
  @param  buf: 返回要读取的数据
  @param  len: 需要读取的数据长度
  @param  offset: 需要读取的flash地址
  @return 返回写入的长度,返回值小于0表示写入失败
  @note
 */
/*----------------------------------------------------------------------------*/
static int dlog_ex_flash_read(void *buf, u16 len, u32 offset)
{
    // 需要实现
    if (dlog_use_ex_flash) {
        _norflash_read(offset, buf, len, 0);
    }
    /* printf("%s, %d", __func__, len); */
    /* put_buf(buf, len); */
    return len;
}

// 返回 0 表示初始化成功
static int dlog_ex_flash_init(void)
{
    int err;

#ifdef CONFIG_CPU_BR36
    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_01), PORT_OUTPUT_HIGH);
#endif
#ifdef CONFIG_CPU_BR52
    gpio_set_mode(IO_PORT_SPILT(IO_PORTB_04), PORT_OUTPUT_HIGH);
#endif
    os_time_dly(1);
    _norflash_init("flash1", (struct norflash_dev_platform_data *)&spi_flash_platform_data);

    err = _norflash_open(NULL);
    if (err) {
        _norflash_close();
        gpio_set_mode(IO_PORT_SPILT(IO_PORTC_01), PORT_HIGHZ);
    } else {
        dlog_use_ex_flash = 1;
    }
    //sys_timeout_add(NULL, dlog_print_test, 3000);
    return 0;
}

REGISTER_DLOG_OPS(ex_flash_op, 1) = {
    .dlog_output_init       = dlog_ex_flash_init,
    .dlog_output_get_zone   = dlog_get_ex_flash_zone,
    .dlog_output_zone_erase = dlog_ex_flash_zone_erase,
    .dlog_output_write      = dlog_ex_flash_write,
    .dlog_output_read       = dlog_ex_flash_read,
    .dlog_output_direct     = dlog_output_direct,
};

#else  // 内置flash


#define DLOG_FILE_PATH  		          "flash/app/dlog"

struct inside_flash_info_s {
    u32 addr;
    u32 len;
};

static struct inside_flash_info_s flash_info;

static int dlog_get_inside_flash_zone(u32 *addr, u32 *len)
{
    if (addr) {
        *addr = flash_info.addr;
    }
    if (len) {
        *len = flash_info.len;
    }

    return 0;
}

static int dlog_inside_flash_zone_erase(u16 erase_sector, u16 sector_num)
{
    int ret = -1;

    if ((flash_info.addr == 0) || (flash_info.len == 0)) {
        return ret;
    }

    u32 erase_addr = sdfile_cpu_addr2flash_addr(flash_info.addr + erase_sector * LOG_BASE_UNIT_SIZE);
    u32 erase_unit = LOG_BASE_UNIT_SIZE;
    u32 erase_size = 0;
    u32 total_size = LOG_BASE_UNIT_SIZE * sector_num;
    u32 erase_cmd = IOCTL_ERASE_SECTOR;
    if (boot_info.vm.align == 1) {
        erase_unit = 256;
        erase_cmd = IOCTL_ERASE_PAGE;
    }
    do {
        ret = norflash_ioctl(NULL, erase_cmd, erase_addr);
        if (ret < 0) {
            break;
        }
        erase_addr += erase_unit;
        erase_size += erase_unit;
    } while (erase_size < total_size);

    return ret;
}

static int dlog_inside_flash_init(void)
{
    RESFILE *fp = resfile_open(DLOG_FILE_PATH);
    if (fp == NULL) {
        printf("Open %s dlog file fail", DLOG_FILE_PATH);
        return -1;
    }

    struct resfile_attrs file_attr = {0};
    resfile_get_attrs(fp, &file_attr);
    flash_info.addr = file_attr.sclust;
    flash_info.len = file_attr.fsize;

    if (fp) {
        resfile_close(fp);
    }

    printf("dlog flash init ok!\n");
    printf("dlog file addr 0x%x, len %d\n", flash_info.addr, flash_info.len);

    return 0;
}

static int dlog_inside_flash_write(void *buf, u16 len, u32 offset)
{
    int ret = -1;

    if ((flash_info.addr == 0) || (flash_info.len == 0)) {
        return ret;
    }

    u32 write_addr = sdfile_cpu_addr2flash_addr(offset); //flash addr
    printf("flash write addr 0x%x\n", write_addr);
    //写flash:
    ret = norflash_write(NULL, (void *)buf, len, write_addr);
    if (ret != len) {
        printf("dlog data save to flash err\n");
    }

    return ret;
}

static int dlog_inside_flash_read(void *buf, u16 len, u32 offset)
{
    memcpy(buf, (u8 *)offset, len);

    return len;
}

REGISTER_DLOG_OPS(ex_flash_op, 0) = {
    .dlog_output_init       = dlog_inside_flash_init,
    .dlog_output_get_zone   = dlog_get_inside_flash_zone,
    .dlog_output_zone_erase = dlog_inside_flash_zone_erase,
    .dlog_output_write      = dlog_inside_flash_write,
    .dlog_output_read       = dlog_inside_flash_read,
    .dlog_output_direct     = dlog_output_direct,
};

#endif
