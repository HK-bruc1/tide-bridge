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
 Date: 2025-06-18 13:00:13
 LastEditors: sheng.dong
 LastEditTime: 2025-06-18 13:00:31
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_spi.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/
#include "stdint.h" 
#include "stdlib.h"
#include "sdk_config.h"
#include "app_msg.h"
#include "app_main.h"
#include "multi_protocol_main.h"
#include "typedef.h"
#include "os/os_api.h"
#include "circular_buf.h"
#include "jlstream.h"
#include "gpio_config.h"
#include "rdx_board_config.h"
#include "rdx_board_hal.h"
#include "rdx_port_spi.h"
#include "rdx_wifi_service.h"

#include "media/includes.h"
#include "spi.h"
#include "generic/log.h"
#include "clock.h"
#include "asm/wdt.h"

#include "rdx_commonDef.h"
#include "rdx_common.h"
#include "rdx_spi.h"
#include "rdx_uxfile.h"
#include "xxpUart.h"
#include "rdx_app.h"
#include "rdx_protocol.h"


/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#if 1
#define esp_log(str, ...)                       y_printf("[esp]%s,L%d," str,__FUNCTION__,__LINE__, ##__VA_ARGS__)
#else
#define esp_log(str, ...)
#endif

#define SPI_DMA_TIMEOUT_CNT                             500000
#define SPI_EXCEPTION_MAX_RETRIES                       5

/* SPI pins now sourced from rdx_board_get_config(), see board/<name>/rdx_board_config.c */



#define SPI_CMD_LEN                             (1)
#define SPI_ADDR_LEN                            (1)
#define SPI_DUMMY_LEN                           (1)

#define SPI_SLAVE_STATUS_LEN                    (4)
#define SPI_SLAVE_DATA_INFO                     (4)

#define SPI_TX_RX_ISR                           (1)

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
typedef enum {
    SPI_NULL = 0,
    SPI_READ,         // slave -> master
    SPI_WRITE,        // maste -> slave
} spi_mode_t;

typedef struct {
    bool slave_notify_flag; // when slave recv done or slave notify master to recv, it will be true
} spi_master_msg_t;

typedef struct {
    uint32_t     magic    : 8;    // 0xFE
    uint32_t     send_seq : 8;
    uint32_t     send_len : 16;
} spi_send_opt_t;

typedef struct {
    uint32_t     direct : 8;
    uint32_t     seq_num : 8;
    uint32_t     transmit_len : 16;
} spi_recv_opt_t;

typedef struct {
    spi_mode_t direct;
} spi_msg_t;

/**
 * This structure describes one SPI transaction. The descriptor should not be modified until the transaction finishes.
 */
typedef struct {
    uint32_t flags;                 ///< Bitwise OR of SPI_TRANS_* flags
    uint16_t cmd;                   /**< Command data, of which the length is set in the ``command_bits`` of spi_device_interface_config_t.
                                      *
                                      *  <b>NOTE: this field, used to be "command" in ESP-IDF 2.1 and before, is re-written to be used in a new way in ESP-IDF 3.0.</b>
                                      *
                                      *  Example: write 0x0123 and command_bits=12 to send command 0x12, 0x3_ (in previous version, you may have to write 0x3_12).
                                      */
    uint64_t addr;                  /**< Address data, of which the length is set in the ``address_bits`` of spi_device_interface_config_t.
                                      *
                                      *  <b>NOTE: this field, used to be "address" in ESP-IDF 2.1 and before, is re-written to be used in a new way in ESP-IDF3.0.</b>
                                      *
                                      *  Example: write 0x123400 and address_bits=24 to send address of 0x12, 0x34, 0x00 (in previous version, you may have to write 0x12340000).
                                      */
    size_t length;                  ///< Total data length, in bits
    size_t rxlength;                ///< Total data length received, should be not greater than ``length`` in full-duplex mode (0 defaults this to the value of ``length``).
    void *user;                     ///< User-defined variable. Can be used to store eg transaction ID.
    union {
        const void *tx_buffer;      ///< Pointer to transmit buffer, or NULL for no MOSI phase
        uint8_t tx_data[4];         ///< If SPI_TRANS_USE_TXDATA is set, data set here is sent directly from this variable.
    };
    union {
        void *rx_buffer;            ///< Pointer to receive buffer, or NULL for no MISO phase. Written by 4 bytes-unit if DMA is used.
        uint8_t rx_data[4];         ///< If SPI_TRANS_USE_RXDATA is set, data is received directly to this variable
    };
}spi_transaction_t;        //the rx data should start from a 32-bit aligned address to get around dma issue.

typedef struct{
    bool spi_init_flag;
    cbuffer_t spi_master_tx_ring_buf;
    u8* send_buf;
    OS_MUTEX pxMutex;
    OS_SEM spi_send_sem;
    u8 trans_data[ESP_SPI_DMA_MAX_LEN + 1];
    u8 initiative_send_flag; // it means master has data to send to slave
    u32 plan_send_len; // master plan to send data len
    u8 current_send_seq;
    u8 current_recv_seq;
}spi_master_manager;

/******************************************************************************
* Global Variables Section
******************************************************************************/ 


/******************************************************************************
* Local Variables Section
******************************************************************************/ 
static esp8684_param esp8684_info;

#define spi_cs_init()       rdx_port_spi_cs_init(rdx_board_get_config()->spi_cs_io)
#define spi_cs_uninit()     rdx_port_spi_cs_uninit(rdx_board_get_config()->spi_cs_io)
#define spi_cs_h()          gpio_set_mode(IO_PORT_SPILT(rdx_board_get_config()->spi_cs_io), PORT_OUTPUT_HIGH)
#define spi_cs_l()          gpio_set_mode(IO_PORT_SPILT(rdx_board_get_config()->spi_cs_io), PORT_OUTPUT_LOW)
#define spi_read_byte()             spi_recv_byte(esp8684_info.spi_hdl, NULL)
#define spi_write_byte(x)           spi_send_byte(esp8684_info.spi_hdl, x)
#define spi_dma_read(x, y)          spi_dma_recv(esp8684_info.spi_hdl, x, y)
#define spi_dma_write(x, y)         spi_dma_send(esp8684_info.spi_hdl, x, y)
#define spi_set_width(x)            spi_set_bit_mode(esp8684_info.spi_hdl, x)
#define spi_init(cfg)               spi_open(esp8684_info.spi_hdl, cfg)
#define spi_closed()                spi_deinit(esp8684_info.spi_hdl)
#define spi_suspend()               hw_spi_suspend(esp8684_info.spi_hdl)
#define spi_resume()                hw_spi_resume(esp8684_info.spi_hdl)

static const char* TAG = "SPI AT Master";

static spi_master_manager spi_manager;
static volatile u8 spi_handshake_missed = 0;
static u8 spi_exception_retry_cnt = 0;
static u32 spi_send_start_ts = 0;
#define SPI_SEND_STUCK_TIMEOUT_MS   5000

#ifndef RDX_SPI_VERBOSE_TRACE
#define RDX_SPI_VERBOSE_TRACE   0
#endif

#if RDX_SPI_VERBOSE_TRACE
#define spi_trace(...)  r_printf(__VA_ARGS__)
#else
#define spi_trace(...)  do { } while (0)
#endif


/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
extern u8 xxp_rx_parse(u8 *data, unsigned short len);
extern void xxp_wifi_tcp_file_stop_indicate(void);

static void gpio_handshake_isr_handler(void* arg);

static rdx_spi_rx_cb_t      g_spi_rx_cb      = NULL;
static rdx_spi_tx_done_cb_t g_spi_tx_done_cb = NULL;
static void                 *g_spi_cb_ctx     = NULL;

void rdx_spi_register_wifi_callbacks(rdx_spi_rx_cb_t rx_cb,
                                     rdx_spi_tx_done_cb_t tx_done_cb,
                                     void *ctx)
{
    g_spi_rx_cb      = rx_cb;
    g_spi_tx_done_cb = tx_done_cb;
    g_spi_cb_ctx     = ctx;
}

static struct _p33_io_wakeup_config gpio_irq_config_esp = {
    .pullup_down_mode = PORT_INPUT_PULLDOWN_1M,
    .filter      		= PORT_FLT_DISABLE,
    .edge               = PORT_IRQ_EDGE_RISE,
    .gpio               = 0,  /* set at runtime from board config */
    .callback			= gpio_handshake_isr_handler,
};


/******************************************************************************
* Function Section
******************************************************************************/ 
static void spi_mutex_lock(void)
{
    // r_printf(" ===== %s --> lock 0 \r", __FUNCTION__);
    os_mutex_pend(&spi_manager.pxMutex, 0);
    // r_printf(" ===== %s --> lock 1 \r", __FUNCTION__);
}

static void spi_mutex_unlock(void)
{
    // r_printf(" ===== %s --> unlock \r", __FUNCTION__);
    os_mutex_post(&spi_manager.pxMutex);
}


/*
This isr handler is called when the handshake line goes high.
There are two ways to trigger the GPIO interrupt:
1. Master sends data, slave has received successfully
2. Slave has data want to transmit
*/
static void gpio_handshake_isr_handler(void* arg)
{
    spi_master_msg_t spi_msg = {
        .slave_notify_flag = true,
    };

    int ret = os_taskq_post_msg("spi_trans_task", 2, SPI_MSG_SLAVE_NOTIFY, spi_msg.slave_notify_flag);
    if (ret != OS_NO_ERR) {
        spi_handshake_missed = 1;
    }
}


static void spi_wait_dma_tx_done(void)
{
    int cnt = 0;
    while (hw_spix_get_isr_status(esp8684_info.spi_hdl) != SPI_TX_FINISH) {
        if (++cnt > SPI_DMA_TIMEOUT_CNT) {
            r_printf("!!! SPI DMA TX timeout\n");
            wdt_clear();
            return;
        }
    }
}

static void spi_wait_dma_rx_done(void)
{
    int cnt = 0;
    while (hw_spix_get_isr_status(esp8684_info.spi_hdl) != SPI_RX_FINISH) {
        if (++cnt > SPI_DMA_TIMEOUT_CNT) {
            r_printf("!!! SPI DMA RX timeout\n");
            wdt_clear();
            return;
        }
    }
}

static int spi_device_polling_transmit(u8 handle, spi_transaction_t *trans)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 dummy = 0x00;

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // b_printf("=== %s --> cmd: %02x, addr: %x, length: %d, rxlength: %d, user: %x", __func__, trans->cmd, trans->addr, trans->length, trans->rxlength, trans->user);
    // spi_mutex_lock();
    spi_cs_l();
    switch(trans->cmd){
        case CMD_HD_WRDMA_REG:
            {
                /*
                    CMD  | ADDR | DUMMY | DATA (高达 4092 字节)
                    0x03 | 0x00 | 0x00  | data_buffer 
                */
                //主机向从机发送数据
                u32 total_len = trans->length/8 + SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN;
                u8 *temp = malloc(total_len + 1);
                if(!temp){
                    EXCEPTION_THROW();
                }
                memset(temp, 0, total_len + 1);
                temp[0] = trans->cmd;
                temp[1] = 0x00;
                temp[2] = dummy;
                memcpy(temp + SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN, trans->tx_buffer, trans->length/8);

            #if (SPI_TX_RX_ISR == 1)
                spi_dma_transmit_for_isr(esp8684_info.spi_hdl, temp, total_len, 0);
                spi_wait_dma_tx_done();
            #else
                spi_dma_send(esp8684_info.spi_hdl, temp, total_len);
            #endif
                free(temp);
            }
            break;

        case CMD_HD_WR_END_REG: 
            {
                /*
                    主机通知从机数据发送结束：
                    CMD | ADDR | DUMMY | DATA
                    0x07| 0x00 | 0x00   | null
                */
                u8 temp[SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN];
                temp[0] = trans->cmd;
                temp[1] = 0x00;
                temp[2] = dummy;

                u32 total_len = SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN;
                // b_printf("=== write dma --> CMD: %02X, len: %d, %s \r", trans->cmd, total_len, trans->tx_buffer);
            #if (SPI_TX_RX_ISR == 1)
                spi_dma_transmit_for_isr(esp8684_info.spi_hdl, temp, total_len, 0);
                spi_wait_dma_tx_done();
            #else
                spi_dma_send(esp8684_info.spi_hdl, temp, total_len);
            #endif
            }
            break;

        case CMD_HD_RDDMA_REG:
            {
                /*
                    CMD  | ADDR | DUMMY | DATA (高达 4092 字节)
                    0x04 | 0x00 | 0x00  | data_buffer 
                */
                //主机读取从机发送的数据
                u8 temp[SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN];
                temp[0] = trans->cmd;
                temp[1] = 0x00;
                temp[2] = dummy;
                u32 total_len = SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN;
            #if (SPI_TX_RX_ISR == 1)
                spi_dma_transmit_for_isr(esp8684_info.spi_hdl, temp, total_len, 0);
                spi_wait_dma_tx_done();
            #else
                spi_dma_send(esp8684_info.spi_hdl, temp, total_len);            
            #endif
            #if (SPI_TX_RX_ISR == 1)
                spi_dma_transmit_for_isr(esp8684_info.spi_hdl, trans->rx_buffer, trans->rxlength/8, 1); 
                spi_wait_dma_rx_done();
            #else
                spi_dma_recv(esp8684_info.spi_hdl, trans->rx_buffer, trans->rxlength/8);
            #endif
                
            }
            break;

        case CMD_HD_INT0_REG: 
            {
                /*
                    主机通知从机数据接收结束：
                    CMD | ADDR | DUMMY | DATA
                    0x08| 0x00 | 0x00   | null
                */
                u8 temp[SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN];
                temp[0] = trans->cmd;
                temp[1] = 0x00;
                temp[2] = dummy;
                u32 total_len = SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN;
                // put_buf(temp, total_len);
            #if (SPI_TX_RX_ISR == 1)
                spi_dma_transmit_for_isr(esp8684_info.spi_hdl, temp, total_len, 0);
                spi_wait_dma_tx_done();
            #else
                spi_dma_send(esp8684_info.spi_hdl, temp, total_len);
            #endif
            }
            break;

        case CMD_HD_WRBUF_REG:
            {
                /*
                    CMD  | ADDR | DUMMY | DATA (4 字节)
                    0x01 | 0x00 | 0x00  | data_info 
                */
                //主机向从机发送写数据请求
                spi_send_opt_t *s_send_opt = (spi_send_opt_t*)trans->tx_buffer;
                u32 total_len = SPI_SLAVE_DATA_INFO + SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN;
                u8 temp[total_len + 1];
                memset(temp, 0, total_len + 1);
                temp[0] = trans->cmd;
                temp[1] = trans->addr;
                temp[2] = dummy;
                temp[3] = s_send_opt->magic;
                temp[4] = s_send_opt->send_seq;
                temp[5] = GET_LOWBYTE(s_send_opt->send_len);
                temp[6] = GET_HIGHBYTE(s_send_opt->send_len);
                // put_buf(temp, total_len);
            #if (SPI_TX_RX_ISR == 1)
                spi_dma_transmit_for_isr(esp8684_info.spi_hdl, temp, total_len, 0);
                spi_wait_dma_tx_done();
            #else
                spi_dma_send(esp8684_info.spi_hdl, temp, total_len);
            #endif
            }
            break;

        case CMD_HD_RDBUF_REG:
            {
                /*
                    CMD  | ADDR | DUMMY | DATA (4 字节)
                    0x02 | 0x04 | 0x00  | slave_status
                */
                //主机发送请求，查询从机的可读/可写状态
                u8 temp[10];
                memset(temp, 0, 10);
                temp[0] = trans->cmd;
                temp[1] = trans->addr;
                temp[2] = dummy;
                u32 total_len = SPI_CMD_LEN + SPI_ADDR_LEN + SPI_DUMMY_LEN;
                // put_buf(temp, total_len);
            #if (SPI_TX_RX_ISR == 1)
                spi_dma_transmit_for_isr(esp8684_info.spi_hdl, temp, total_len, 0);
                spi_wait_dma_tx_done();
                spi_dma_transmit_for_isr(esp8684_info.spi_hdl, trans->rx_buffer, trans->rxlength/8, 1); 
                spi_wait_dma_rx_done();
            #else
                spi_dma_send(esp8684_info.spi_hdl, temp, total_len);
                spi_dma_recv(esp8684_info.spi_hdl, trans->rx_buffer, trans->rxlength/8);
            #endif
            }
            break;

        default:
            break;
    }

    spi_cs_h();
    // spi_mutex_unlock();
    return 0;

EXCEPTION_POINTER()

    spi_cs_h();
    return -1;
}

static void at_spi_master_send_data(uint8_t* data, uint32_t len)
{
    spi_transaction_t trans = {
#if defined(CONFIG_SPI_QUAD_MODE)
        .flags = SPI_TRANS_MODE_QIO,
        .cmd = CMD_HD_WRDMA_REG | (0x2 << 4),    // master -> slave command, donnot change
#elif defined(CONFIG_SPI_DUAL_MODE)
        .flags = SPI_TRANS_MODE_DIO,
        .cmd = CMD_HD_WRDMA_REG | (0x1 << 4),
#else
        .cmd = CMD_HD_WRDMA_REG,    // master -> slave command, donnot change
#endif
        .length = len * 8,
        .tx_buffer = (void*)data
    };
    spi_device_polling_transmit(esp8684_info.spi_hdl, &trans);
}

static void at_spi_master_recv_data(uint8_t* data, uint32_t len)
{
    spi_transaction_t trans = {
#if defined(CONFIG_SPI_QUAD_MODE)
        .flags = SPI_TRANS_MODE_QIO,
        .cmd = CMD_HD_RDDMA_REG | (0x2 << 4),    // master -> slave command, donnot change
#elif defined(CONFIG_SPI_DUAL_MODE)
        .flags = SPI_TRANS_MODE_DIO,
        .cmd = CMD_HD_RDDMA_REG | (0x1 << 4),
#else
        .cmd = CMD_HD_RDDMA_REG,    // master -> slave command, donnot change
#endif
        .rxlength = len * 8,
        .rx_buffer = (void*)data
    };
    spi_device_polling_transmit(esp8684_info.spi_hdl, &trans);
}

// send a single to slave to tell slave that master has read DMA done
static void at_spi_rddma_done(void)
{
    spi_transaction_t end_t = {
        .cmd = CMD_HD_INT0_REG,
    };
    spi_device_polling_transmit(esp8684_info.spi_hdl, &end_t);
}

// send a single to slave to tell slave that master has write DMA done
static void at_spi_wrdma_done(void)
{
    spi_transaction_t end_t = {
        .cmd = CMD_HD_WR_END_REG,
    };
    spi_device_polling_transmit(esp8684_info.spi_hdl, &end_t);
}

// when spi slave ready to send/recv data from the spi master, the spi slave will a trigger GPIO interrupt,
// then spi master should query whether the slave will perform read or write operation.
static spi_recv_opt_t query_slave_data_trans_info()
{
    spi_recv_opt_t recv_opt;
    spi_transaction_t trans = {
        .cmd = CMD_HD_RDBUF_REG,
        .addr = RDBUF_START_ADDR,
        .rxlength = 4 * 8,
        .rx_buffer = &recv_opt,
    };

    spi_device_polling_transmit(esp8684_info.spi_hdl, (spi_transaction_t*)&trans);
    // put_buf(trans.tx_buffer, trans.rxlength/8);
    {
        const uint8_t *raw = (const uint8_t *)&recv_opt;
        spi_trace("=== RDBUF raw: %02X %02X %02X %02X (direct=%u seq=%u len=%u)\r",
                  raw[0], raw[1], raw[2], raw[3],
                  recv_opt.direct, recv_opt.seq_num, recv_opt.transmit_len);
    }
    return recv_opt;
}

// before spi master write to slave, the master should write WRBUF_REG register to notify slave,
// and then wait for handshark line trigger gpio interrupt to start the data transmission.
static void spi_master_request_to_write(uint8_t send_seq, uint16_t send_len)
{
    spi_send_opt_t send_opt;
    send_opt.magic = 0xFE;
    send_opt.send_seq = send_seq;
    send_opt.send_len = send_len;

    spi_transaction_t trans = {
        .cmd = CMD_HD_WRBUF_REG,
        .addr = WRBUF_START_ADDR,
        .length = 4 * 8,
        .tx_buffer = &send_opt,
    };
    spi_device_polling_transmit(esp8684_info.spi_hdl, (spi_transaction_t*)&trans);
    // increment
    spi_manager.current_send_seq  = send_seq;
}

// spi master write data to slave
static int8_t spi_write_data(uint8_t* buf, int32_t len)
{
    if (len > ESP_SPI_DMA_MAX_LEN) {
        esp_log("Send length errot, len:%ld", len);
        return -1;
    }
    at_spi_master_send_data(buf, len);
    at_spi_wrdma_done();
    return 0;
}

// write data to spi tx_ring_buf, this is just for test
static u32 write_data_to_spi_task_tx_ring_buf(const void* data, size_t size)
{
    u32 length = size;

    if (data == NULL  || length > STREAM_BUFFER_SIZE) {
        esp_log("Write data error, len:%ld", length);
        return -1;
    }

    length = cbuf_write(&spi_manager.spi_master_tx_ring_buf, data, size);
    return length;
}

static void spi_wait_handshake_low(void)
{
    int wait = 0;
    while (gpio_read(rdx_board_spi_handshake_io()) && wait < 500) {
        udelay(10);
        wdt_clear();
        wait++;
    }
}

// notify slave to recv data
static void notify_slave_to_recv(void)
{
    // y_printf("=====> notify_slave_to_recv, initiative_send_flag = %d \r", spi_manager.initiative_send_flag);
    if (spi_manager.initiative_send_flag == 0) {
        spi_mutex_lock();
        uint32_t tmp_send_len = cbuf_get_data_size(&spi_manager.spi_master_tx_ring_buf);
        if (tmp_send_len > 0) {
            spi_wait_handshake_low();
            spi_manager.plan_send_len = tmp_send_len > ESP_SPI_DMA_MAX_LEN ? ESP_SPI_DMA_MAX_LEN : tmp_send_len;
            spi_trace("=== master->slave WRBUF: seq=%u, plan_len=%u (cbuf=%u, hs=%d)\r",
                      (unsigned)(spi_manager.current_send_seq + 1),
                      (unsigned)spi_manager.plan_send_len,
                      (unsigned)tmp_send_len,
                      gpio_read(rdx_board_spi_handshake_io()));
            spi_master_request_to_write(spi_manager.current_send_seq + 1, spi_manager.plan_send_len); // to tell slave that the master want to write data
            spi_manager.initiative_send_flag = 1;
            spi_send_start_ts = jiffies_msec();
        }
        spi_mutex_unlock();
    }
}

void rdx_spi_stop_send(void)
{
    // [新增] 检查SPI是否已初始化，避免访问无效内存
    if(spi_manager.spi_init_flag == false){
        // r_printf("[SPI] rdx_spi_stop_send: SPI not initialized, skip\n");
        return;
    }
    
    // spi_manager.current_send_seq = 0;
    // spi_manager.plan_send_len = 0;

    spi_mutex_lock();

    spi_recv_opt_t recv_opt = query_slave_data_trans_info();
    esp_log("now direct: %u \r", recv_opt.direct);

    if (recv_opt.direct == SPI_READ) { // if slave in waiting response status, master need to give a read done single.
        if (recv_opt.seq_num != ((spi_manager.current_recv_seq + 1) & 0xFF)) {
            esp_log("SPI recv seq error, %x, %x \r", recv_opt.seq_num, (spi_manager.current_recv_seq + 1));
            if (recv_opt.seq_num == 1) {
                esp_log("Maybe SLAVE restart, ignore \r");
            }
        }

        spi_manager.current_recv_seq = recv_opt.seq_num;

        at_spi_rddma_done();
    }else{
        esp_log("SPI wrtie done, current_send_seq = %x \r", spi_manager.current_send_seq);
        at_spi_wrdma_done();
    }
    cbuf_clear(&spi_manager.spi_master_tx_ring_buf);
    memset(spi_manager.send_buf, 0, STREAM_BUFFER_SIZE + 1);
    spi_manager.initiative_send_flag = 0;
    memset(spi_manager.trans_data, 0, ESP_SPI_DMA_MAX_LEN + 1);
    spi_exception_retry_cnt = 0;
    spi_send_start_ts = 0;

    spi_mutex_unlock();
    // esp_log("================ rdx_spi_stop_send ok \n");
}

/**************************************************************************
 * function: rdx_esp32_spi_send_data
 * description:
 * param (u8) *data
 * param (u32) len
 * return (*)
 **************************************************************************/
int rdx_esp32_spi_send_data(u8 *data, u32 len)
{ 
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u32 wlen = 0;
    u32 retries = 0;
    const u32 max_retries = 50;
    u32 delay_ms = 5;
    RdxWifiInfo* pw = rdx_app_get_wifi_info();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(spi_manager.spi_init_flag == false || pw->onoff == TRANSFER_BY_WIFI_OFF){
        r_printf("===== %s: spi not init \r", __func__);
        return 0;
    }
    if (!data || len == 0) {
        r_printf("===== %s: invalid input data or length \r", __func__);
        return 0;
    }

    if (!spi_manager.send_buf) {
        r_printf("===== %s: spi send buffer not allocated \r", __func__);
        return 0;
    }
    // y_printf("=========== %s, len: %d \r", __func__, len);

    while (retries < max_retries) {
        if (pw->onoff == TRANSFER_BY_WIFI_OFF) {
            r_printf("===== %s: wifi closed during wait \r", __func__);
            return 0;
        }
        u32 buf_space = cbuf_is_write_able(&spi_manager.spi_master_tx_ring_buf, len);
        if (buf_space >= len) {
            break;
        }

        if (retries % 10 == 0) {
            r_printf("===== cbuf full, waiting %d/%d (delay %dms) \r", retries+1, max_retries, delay_ms);
        }
        os_time_dly(delay_ms / 10 + 1);
        retries++;
        if (delay_ms < 40) {
            delay_ms += 5;
        }
    }

    if (retries >= max_retries) {
        r_printf("===== %s: cbuf full after %d retries (~1.1s), dropping data! len=%d \r", __func__, max_retries, len);
        return 0;
    }

    wlen = cbuf_write(&spi_manager.spi_master_tx_ring_buf, data, len);
    // esp_log("=== %s, wlen: %d \r", __func__, wlen);

    int r = os_taskq_post_msg("spi_trans_task", 1, SPI_MSG_MASTER_WRITE);
    if (r != OS_NO_ERR) {
        r_printf("=== %s, os_taskq_post_msg failed: %d \r", __func__, r);
    }

    return wlen;
}

/**************************************************************************
 * function: rdx_spi_slave_msg_handler
 * description: 
 * param (bool) flag
 * return (*)
 **************************************************************************/
void rdx_spi_slave_msg_handler(bool flag)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int ret = 0;
    uint32_t send_len = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    spi_mutex_lock();

    spi_recv_opt_t recv_opt = query_slave_data_trans_info();
    // esp_log("===== recv_opt.direct: %d, recv_opt.seq_num: %d \r", recv_opt.direct, recv_opt.seq_num);
    if (recv_opt.direct != SPI_WRITE && recv_opt.direct != SPI_READ) {
        int qretry;
        for (qretry = 0; qretry < 3; qretry++) {
            udelay(100);
            wdt_clear();
            recv_opt = query_slave_data_trans_info();
            if (recv_opt.direct == SPI_WRITE || recv_opt.direct == SPI_READ) break;
        }
        if (recv_opt.direct != SPI_WRITE && recv_opt.direct != SPI_READ) {
            r_printf("!!! SPI query unknown direct=%d after retries "
                     "(init_flag=%d, plan_len=%u, seq=%u, len=%u)\n",
                     recv_opt.direct, spi_manager.initiative_send_flag,
                     (unsigned)spi_manager.plan_send_len,
                     recv_opt.seq_num, recv_opt.transmit_len);
            EXCEPTION_THROW();
        }
    }
    if (recv_opt.direct == SPI_WRITE) {
        // b_printf("===== SPI WRITE --> transmit_len: %d, plan_send_len: %d \r", recv_opt.transmit_len, spi_manager.plan_send_len);
        if (spi_manager.plan_send_len == 0) {
            esp_log("master want send data but length is 0");
            EXCEPTION_THROW();
        }
        if (recv_opt.seq_num != spi_manager.current_send_seq) {
            esp_log("SPI send seq error, %x, %x", recv_opt.seq_num, spi_manager.current_send_seq);
            if (recv_opt.seq_num == 1) {
                esp_log("Maybe SLAVE restart, ignore");
                spi_manager.current_send_seq = recv_opt.seq_num;
            } else {
                esp_log("SPI send seq error, %x, %x", recv_opt.seq_num, spi_manager.current_send_seq);
                EXCEPTION_THROW();
            }
        }
        send_len = cbuf_read(&spi_manager.spi_master_tx_ring_buf, (void*) spi_manager.trans_data, spi_manager.plan_send_len); 
        if (send_len != spi_manager.plan_send_len) {
            esp_log("Read len expect: %lu, but actual read: %lu \n", spi_manager.plan_send_len, send_len);
            EXCEPTION_THROW();
        }
        ret = spi_write_data(spi_manager.trans_data, spi_manager.plan_send_len);
        if (ret < 0) {
            esp_log("Load data error");
            EXCEPTION_THROW();
        }
        // throttle: give ESP8684 time to forward SPI data via TCP
        udelay(200);
        // maybe streambuffer filled some data when SPI transimit, just consider it after send done, because send flag has already in SLAVE queue
        uint32_t tmp_send_len = cbuf_get_data_size(&spi_manager.spi_master_tx_ring_buf);
        if (tmp_send_len > 0) {
            spi_wait_handshake_low();
            spi_manager.plan_send_len = tmp_send_len > ESP_SPI_DMA_MAX_LEN ? ESP_SPI_DMA_MAX_LEN : tmp_send_len;
            spi_master_request_to_write(spi_manager.current_send_seq + 1, spi_manager.plan_send_len);
            // b_printf(" still have data in spi_master_tx_ring_buf, tmp_send_len: %d, plan_send_len: %d \r", tmp_send_len, spi_manager.plan_send_len);
            // os_time_dly(1);
        } else {
            spi_manager.initiative_send_flag = 0;
            spi_exception_retry_cnt = 0;
            spi_send_start_ts = 0;

            if (g_spi_tx_done_cb) {
                g_spi_tx_done_cb(g_spi_cb_ctx);
            }
        }
        spi_mutex_unlock();
    } else if (recv_opt.direct == SPI_READ) {
        // y_printf("===== SPI READ -->  transmit_len: %d \r", recv_opt.transmit_len);
        if (recv_opt.seq_num != ((spi_manager.current_recv_seq + 1) & 0xFF)) {
            esp_log("SPI recv seq error, %x, %x \n", recv_opt.seq_num, (spi_manager.current_recv_seq + 1));
            if (recv_opt.seq_num == 1) {
                esp_log("Maybe SLAVE restart, ignore");
            } else {
                EXCEPTION_THROW();
            }
        }
        if (recv_opt.transmit_len > STREAM_BUFFER_SIZE || recv_opt.transmit_len == 0) {
            esp_log("SPI read len error, %x", recv_opt.transmit_len);
            EXCEPTION_THROW();
        }
        spi_manager.current_recv_seq = recv_opt.seq_num;
        memset(spi_manager.trans_data, 0, ESP_SPI_DMA_MAX_LEN);
        at_spi_master_recv_data(spi_manager.trans_data, recv_opt.transmit_len);
        at_spi_rddma_done();
        // spi_manager.trans_data[recv_opt.transmit_len] = '\0';
        // printf("%s", trans_data);
        // fflush(stdout);    //Force to print even if have not '\n'

        spi_mutex_unlock();

        if (g_spi_rx_cb) {
            g_spi_rx_cb(spi_manager.trans_data, recv_opt.transmit_len, g_spi_cb_ctx);
        }
    } else {
        esp_log("Unknow direct: %d \n", recv_opt.direct);
        EXCEPTION_THROW();
    }
    return;

EXCEPTION_POINTER()
    {
        r_printf("!!! SPI exception! flag=%d, plan_len=%d, seq=%d\n",
                 spi_manager.initiative_send_flag, spi_manager.plan_send_len,
                 spi_manager.current_send_seq);
        spi_manager.initiative_send_flag = 0;
        spi_manager.plan_send_len = 0;
        cbuf_clear(&spi_manager.spi_master_tx_ring_buf);
        memset(spi_manager.trans_data, 0, ESP_SPI_DMA_MAX_LEN + 1);
        spi_mutex_unlock();

        if (rdx_wifi_service_is_send_stopped()) {
            rdx_wifi_service_on_tx_done();
        } else if (g_spi_tx_done_cb) {
            g_spi_tx_done_cb(g_spi_cb_ctx);
        }
    }
}

/**************************************************************************
 * function: spi_trans_task
 * description: 
 * param (void*) arg
 * return (*)
 **************************************************************************/
static void spi_trans_task(void* arg)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int msg[16];
    // spi_master_msg_t trans_msg = {0};
    int ret = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    esp_log("===> %s", __FUNCTION__);
    // clock_lock("spi_trans_task", 160 * 1000000);
    // clock_lock("sys", 160 * 1000000);
    clock_lock_dump();

    while (1) {
        ret = os_taskq_pend(NULL, msg, ARRAY_SIZE(msg));
        if (ret == OS_TASKQ && msg[0] == Q_MSG) {
            if(msg[1] == SPI_MSG_MASTER_WRITE){
                if (spi_manager.initiative_send_flag == 0) {
                    notify_slave_to_recv();
                } else if (spi_send_start_ts > 0) {
                    u32 elapsed = jiffies_msec() - spi_send_start_ts;
                    if (elapsed > SPI_SEND_STUCK_TIMEOUT_MS) {
                        r_printf("!!! SPI send stuck %dms! Force reset\n", elapsed);
                        spi_mutex_lock();
                        spi_manager.initiative_send_flag = 0;
                        spi_manager.plan_send_len = 0;
                        cbuf_clear(&spi_manager.spi_master_tx_ring_buf);
                        memset(spi_manager.trans_data, 0, ESP_SPI_DMA_MAX_LEN + 1);
                        spi_mutex_unlock();
                        spi_send_start_ts = 0;
                        rdx_wifi_service_retry_on_stuck();
                    }
                }
            }else if(msg[1] == SPI_MSG_SLAVE_NOTIFY){
                spi_trace("=== handshake notify, hs=%d\r",
                          gpio_read(rdx_board_spi_handshake_io()));
                rdx_spi_slave_msg_handler(msg[2]);
            }
        }
        if (spi_handshake_missed) {
            spi_handshake_missed = 0;
            if (gpio_read(rdx_board_spi_handshake_io())) {
                rdx_spi_slave_msg_handler(true);
            }
        }
    }
}

/**************************************************************************
 * function: rdx_spi_task_create
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_spi_task_create(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    int recv_err = os_task_create(spi_trans_task, NULL, 5, 1024, 2048, "spi_trans_task");
    if (recv_err != OS_NO_ERR) {
        r_printf("$$$$$$ spi_trans_task fail!!!");
        return -EINVAL;
    }
    esp_log("$$$$$$ spi_trans_task success!!!");

    return 0;
}

/**************************************************************************
 * function: rdx_spi_task_free
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_spi_task_free(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    os_task_del("spi_trans_task");
    return 0;
}

void spi_master_isr_callback_tt(hw_spi_dev spi, enum hw_spi_isr_status sta)  //spi isr callback
{
    b_printf("========== %s(), spi:%d, sta:%d", __func__, spi, sta);
}

/**************************************************************************
 * function: rdx_spi_init_master_hd
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_spi_init_master_hd(void)
{   
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    const rdx_board_config_t *cfg = rdx_board_get_config();

    memset(&esp8684_info, 0, sizeof(esp8684_param));
    esp8684_info.spi_hdl = cfg->spi_port;

    gpio_irq_config_esp.gpio = cfg->spi_handshake_io;
    p33_io_wakeup_port_init(&gpio_irq_config_esp);
    p33_io_wakeup_enable(gpio_irq_config_esp.gpio, 0);
    spi_cs_init();

    clock_lock("sys", 160 * 1000000);

    //init bus
    struct spi_platform_data spix_p_data_rdx = {
        .port = {
            cfg->spi_clk_io,
            cfg->spi_mosi_io,
            cfg->spi_miso_io,
            0xff, //d2 any io
            0xff, //d3 any io
            0xff, //cs any io(主机不操作cs)
        },
        .role = SPI_ROLE_MASTER,
        .mode = SPI_MODE_UNIDIR_1BIT,//SPI_MODE_UNIDIR_2BIT,//SPI_MODE_BIDIR_1BIT,
        .bit_mode = SPI_FIRST_BIT_MSB,
        .cpol = 0,//clk level in idle state:0:low,  1:high
        .cpha = 0,//sampling edge:0:first,  1:second
        .ie_en = 1, //ie enbale:0:disable,  1:enable
        .irq_priority = 3,
        .spi_isr_callback = NULL,  //spi isr callback
        .clk  = cfg->spi_clk_hz,
    };

    //do init.
    int ret = spi_open(esp8684_info.spi_hdl, &spix_p_data_rdx);
    if (ret < 0) {
        esp_log("spi master init error(%d)!", ret);
    }
    // spi_init(get_hw_spi_config(esp8684_info.spi_hdl));
    // spi_set_width(SPI_MODE_BIDIR_1BIT);

    p33_io_wakeup_enable(gpio_irq_config_esp.gpio, 1);

    esp_log("=== %s ok \r", __func__);
}
/**************************************************************************
 * function: rdx_spi_uninit_master_hd
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_spi_uninit_master_hd(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    p33_io_wakeup_enable(gpio_irq_config_esp.gpio, 0);

    clock_unlock("sys");

    spi_closed();

    spi_cs_uninit();

    esp_log("=== %s ok \r", __func__);
}

/**************************************************************************
 * function: rdx_spi_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_spi_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    esp_log("======= %s \r", __func__);
    if(spi_manager.spi_init_flag == true){
        return -1;
    }

    memset(&spi_manager, 0, sizeof(spi_master_manager));
    memset(spi_manager.trans_data, 0, ESP_SPI_DMA_MAX_LEN + 1);

    rdx_spi_init_master_hd();

    // Create the tx_buf.
    spi_manager.send_buf = malloc(STREAM_BUFFER_SIZE + 1);
    ASSERT(spi_manager.send_buf);
    memset(spi_manager.send_buf, 0, STREAM_BUFFER_SIZE + 1);
    cbuf_init(&spi_manager.spi_master_tx_ring_buf, spi_manager.send_buf, STREAM_BUFFER_SIZE);

    // Create the mutx.
    os_mutex_create(&spi_manager.pxMutex);

    esp_log("===== %s  ok \r", __FUNCTION__);

    // Create the task.
    rdx_spi_task_create();

    spi_manager.spi_init_flag = true;

    return 0;
}

/**************************************************************************
 * function: rdx_spi_deinit
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_spi_deinit(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    esp_log("======= %s --> spi_init_flag: %d \r", __func__, spi_manager.spi_init_flag);
    if(spi_manager.spi_init_flag == false){
        return -1;
    }

    os_mutex_del(&spi_manager.pxMutex, 0);

    rdx_spi_uninit_master_hd();

    cbuf_clear(&spi_manager.spi_master_tx_ring_buf);
    if(spi_manager.send_buf){
        free(spi_manager.send_buf);
        spi_manager.send_buf = NULL;
    }

    rdx_spi_task_free();

    spi_manager.spi_init_flag = false;

    return 0;
}

/**************************************************************************
 * function: rdx_spi_idle_query
 * description: 低功耗空闲查询，返回1表示SPI空闲可进入低功耗
 * param (*)
 * return (u8) 1:idle, 0:busy
 **************************************************************************/
static u8 rdx_spi_idle_query(void)
{
    if (spi_manager.spi_init_flag == false) {
        return 1;
    }

    if (spi_manager.initiative_send_flag) {
        return 0;
    }

    if (cbuf_get_data_size(&spi_manager.spi_master_tx_ring_buf) > 0) {
        return 0;
    }

    if (rdx_wifi_service_is_file_send_busy()) {
        return 0;
    }

    return 1;
}

REGISTER_LP_TARGET(rdx_spi_lp_target) = {
    .name = "rdx_spi",
    .is_idle = rdx_spi_idle_query,
};
