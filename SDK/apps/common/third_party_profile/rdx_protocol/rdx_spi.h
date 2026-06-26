/*=====================================================================================
 HEADER NAME: rdx_spi.h
 MODULE NAME: application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-06-18 13:00:26
 LastEditors: sheng.dong
 LastEditTime: 2025-06-18 13:02:06
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_spi.h
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#include "system/includes.h"
#include "gpio_config.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define CHIP_JL7018                                     0
#define CHIP_JL7016                                     1
#ifndef CHIP_TYPE
#define CHIP_TYPE                                       CHIP_JL7018
#endif


#define DMA_CHAN                                        SPI_DMA_CH_AUTO
#define ESP_SPI_DMA_MAX_LEN                             (4092 * 2)
#define CMD_HD_WRBUF_REG                                0x01
#define CMD_HD_RDBUF_REG                                0x02
#define CMD_HD_WRDMA_REG                                0x03
#define CMD_HD_RDDMA_REG                                0x04
#define CMD_HD_WR_END_REG                               0x07
#define CMD_HD_INT0_REG                                 0x08
#define WRBUF_START_ADDR                                0x00
#define RDBUF_START_ADDR                                0x04
#define STREAM_BUFFER_SIZE                              (8000 * 15)


#define SPI_MSG_SLAVE_NOTIFY                            (0x01)
#define SPI_MSG_MASTER_WRITE                            (0x10)
#define SPI_MSG_MASTER_STOP                             (0x11)

/******************************************************************************
* Structure and Enum Section
******************************************************************************/
typedef struct {
    u8 spi_hdl;
    u8 spi_cs_pin;
    u8 spi_work_mode;
    u8 port;
    u8 spi_clk;
} esp8684_param;

typedef void (*rdx_spi_rx_cb_t)(const u8 *data, u32 len, void *ctx);
typedef void (*rdx_spi_tx_done_cb_t)(void *ctx);

void rdx_spi_register_wifi_callbacks(rdx_spi_rx_cb_t rx_cb,
                                     rdx_spi_tx_done_cb_t tx_done_cb,
                                     void *ctx);

/******************************************************************************
* Function Section
******************************************************************************/ 
