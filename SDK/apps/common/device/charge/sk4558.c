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
 Date: 2025-01-10 23:54:45
 LastEditors: sheng.dong
 LastEditTime: 2025-05-19 22:57:57
 FilePath: \SDK\apps\common\device\charge\sk4558.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#include "app_main.h"
#include "gpio_config.h"
#include "stdlib.h"
#include "SK4558.h"

#include "clock.h"
#include "asm/wdt.h"

#include "rdx_app_config.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define LOG_TAG             "[iic_sk4558]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

//------------------------------------------------------------------------------

#if 0 //0:软件iic,  1:硬件iic
#define _IIC_USE_HW
#endif
#include "iic_api.h"

#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_01)
#define IIC_SCL_IO                              IO_PORTB_04
#define IIC_SDA_IO                              IO_PORTB_05
#define CHARGE_ENABLE_CTRL_IO                   IO_PORTA_00	
#define CHARGE_ENABLE_CTRL_PIN                  PORT_PIN_0
#define CHARGE_ENABLE_CTRL_PORT                 PORTA

#elif (RDX_BJ_VERSION == BJ_BOARD_VERSION_02) || (RDX_BJ_VERSION == BJ_BOARD_VERSION_03)
#define IIC_SCL_IO                              IO_PORTC_04
#define IIC_SDA_IO                              IO_PORTC_05
#define CHARGE_ENABLE_CTRL_IO                   IO_PORTC_03
#define CHARGE_ENABLE_CTRL_PIN                  PORT_PIN_3
#define CHARGE_ENABLE_CTRL_PORT                 PORTC

#endif

#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_03)
// #define CHARGE_FULL_CHECK_IO                    IO_PORTA_03
// #define CHARGE_FULL_CHECK_PIN                   PORT_PIN_3
// #define CHARGE_FULL_CHECK_PORT                  PORTA

// #define CHARGE_FULL_CHECK_IO                    IO_PORT_DP
// #define CHARGE_FULL_CHECK_PIN                   PORT_PIN_0
// #define CHARGE_FULL_CHECK_PORT                  PORTUSB

#define CHARGE_FULL_CHECK_IO                    IO_PORTB_05
#define CHARGE_FULL_CHECK_PIN                   PORT_PIN_5
#define CHARGE_FULL_CHECK_PORT                  PORTB


#else
// #define CHARGE_FULL_CHECK_IO                    IO_PORT_DM	
// #define CHARGE_FULL_CHECK_PIN                   PORT_PIN_1
// #define CHARGE_FULL_CHECK_PORT                  PORTUSB
#endif


#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_01)
 
#ifdef _IIC_USE_HW
static hw_iic_dev sk4558_iic = 0;

void SK4558_init(void)
{
    struct iic_master_config iic_config_sk4558 = {
        .role = IIC_MASTER,
        .scl_io = IIC_SCL_IO,
        .sda_io = IIC_SDA_IO,
        .io_mode = PORT_INPUT_FLOATING,//上拉或浮空(外接上拉)
        .hdrive = PORT_DRIVE_STRENGT_2p4mA,   //enum GPIO_HDRIVE 0:2.4MA, 1:8MA, 2:26.4MA, 3:40MA
        .master_frequency = 100000, //硬件/软件iic频率不准(hz)
        .io_filter = 1,  //硬件/软件无效
    };

    enum iic_state_enum iic_init_state = iic_init(sk4558_iic, &iic_config_sk4558);
    if (iic_init_state == IIC_OK) {
        log_info("sk4558 iic master init ok");
        y_printf("========== %s --> sk4558 iic master init ok \r", __func__);
        SK4558_chrg_constant_voltage(4200);
        SK4558_chrg_precharge_voltage(2400);
        SK4558_chrg_constant_current(300);
        SK4558_chrg_trickle_current(trickle_current_percent_20);
        SK4558_chrg_termination_current(termination_current_percent_10);
        SK4558_chrg_truly_shut_off(TRUE);
    } else {
        log_error("sk4558 iic master init fail");
    }
}

// void IIC_WriteSlave(uint8_t addr, uint8_t *wd, uint16_t len)
// {
//     i2c_master_write_nbytes_to_device(sk4558_iic, addr, wd, len);
// }

// void IIC_ReadSlave(uint8_t addr, uint8_t *rd, uint16_t len, uint8_t flag)
// {
//     i2c_master_read_nbytes_from_device(sk4558_iic, addr, rd, len);
// }

//Communication
void SK4558_chrg_write(uint8_t reg, uint8_t val) 
{
    // uint8_t data[] = {reg & 0xFF, val & 0xFF};
    // IIC_WriteSlave(SK4558_I2C_SLAVE_ADDR_7bit, data, 2);
    
    int r = i2c_master_write_nbytes_to_device_reg(sk4558_iic, SK4558_I2C_SLAVE_ADDR_8bit, &reg, 1, &val, 1);
    b_printf("%s--> r = %d, reg = %d, val = %d \r", __func__, r, reg, val);

    // i2c_master_write_nbytes_to_device_reg(sk4558_iic, SK4558_I2C_SLAVE_ADDR_8bit, &reg, 1, &val, 1);
}

uint8_t SK4558_chrg_read(uint8_t reg) {
    uint8_t resu;
    // IIC_ReadSlave(SK4558_I2C_SLAVE_ADDR_7bit, reg & 0xFF, &resu, 1);//i2c function must set as repeat start mode

    // i2c_master_read_nbytes_from_device_reg(sk4558_iic, SK4558_I2C_SLAVE_ADDR_8bit, &reg, 1, &resu, 1);

    int r = i2c_master_read_nbytes_from_device_reg(sk4558_iic, SK4558_I2C_SLAVE_ADDR_8bit, &reg, 1, &resu, 1);
    b_printf("%s--> r = %d, read_reg = %d, resu = %d \r", __func__, r, reg, resu);

	return resu;
}

#else


#define SK4558_IIC_DELAY            (50)

static soft_iic_dev sk4558_iic = 0;
uint8_t SK4558_chrg_read(uint8_t reg);

#if 0

void SK4558_init(void)
{
    int retval = 0;
    struct iic_master_config sk4558_iic_cfg = {
        .role = IIC_MASTER,
        .scl_io = IIC_SCL_IO,
        .sda_io = IIC_SDA_IO,
        .io_mode = PORT_INPUT_PULLUP_10K,      //上拉或浮空，如果外部电路没有焊接上拉电阻需要置上拉
        .hdrive = PORT_DRIVE_STRENGT_2p4mA,    //IO口强驱
        .io_filter = 0,                        //软件iic无滤波器
    };

    retval = iic_init(sk4558_iic, &sk4558_iic_cfg);
    if (retval < 0) {
        g_printf("\n !!!!!! open iic for sk4558 err\n");
        return;
    } else {
        g_printf("\n ###### sk4558 iic open succ\n"); 

        SK4558_chrg_constant_voltage(4200);
        SK4558_chrg_precharge_voltage(2400);
        SK4558_chrg_constant_current(300);
        SK4558_chrg_trickle_current(trickle_current_percent_10);
        SK4558_chrg_termination_current(termination_current_percent_2p5);
        SK4558_chrg_truly_shut_off(TRUE);

        u8 tt = 0;
        tt = SK4558_chrg_read(CHRG_REG_SET_CHRG_VOLT);
        g_printf("%s ==========> CHRG_VOLT = %02X \r", __func__, tt);
        tt = SK4558_chrg_read(CHRG_REG_SET_CHRG_CURR);
        g_printf("%s ==========> CHRG_CURR = %02X \r", __func__, tt);
        tt = SK4558_chrg_read(CHRG_REG_SET_CHRG_TERM);
        g_printf("%s ==========> CHRG_TERM = %02X \r", __func__, tt);

    }
}

static u8 IIC_WriteSlave(u8 slave_addr, u8 reg_addr, u8 *buf, u8 len)
{
    u8 i;
    iic_start(sk4558_iic);
    if (0 == iic_tx_byte(sk4558_iic, slave_addr)) {
        iic_stop(sk4558_iic);
        return 0;
    }
    delay_nops(SK4558_IIC_DELAY);
    if (0 == iic_tx_byte(sk4558_iic, reg_addr)) {
        iic_stop(sk4558_iic);
        return 0;
    }

    for (i = 0; i < len; i++) {
        delay_nops(SK4558_IIC_DELAY);
        if (0 == iic_tx_byte(sk4558_iic, buf[i])) {
            iic_stop(sk4558_iic);
            return 0;
        }
    }
    iic_stop(sk4558_iic);
    return i;
}

static u8 IIC_ReadSlave(u8 slave_addr, u8 reg_addr, u8 *buf, u8 len)
{
    u8 i;
    iic_start(sk4558_iic);
    if (0 == iic_tx_byte(sk4558_iic, slave_addr)) {
        iic_stop(sk4558_iic);
        return 0;
    }
    delay_nops(SK4558_IIC_DELAY);
    if (0 == iic_tx_byte(sk4558_iic, reg_addr)) {
        iic_stop(sk4558_iic);
        return 0;
    }
    delay_nops(SK4558_IIC_DELAY);

    iic_start(sk4558_iic);
    if (0 == iic_tx_byte(sk4558_iic, slave_addr + 1)) {
        iic_stop(sk4558_iic);
        return 0;
    }
    for (i = 0; i < len; i++) {
        delay_nops(SK4558_IIC_DELAY);
        if (i == (len - 1)) {
            *buf++ = iic_rx_byte(sk4558_iic, 0);
        } else {
            *buf++ = iic_rx_byte(sk4558_iic, 1);
        }
    }
    iic_stop(sk4558_iic);

    return i;
}

//Communication
void SK4558_chrg_write(uint8_t reg, uint8_t val) 
{
    // uint8_t data[] = {reg & 0xFF, val & 0xFF};
    u8 r = IIC_WriteSlave(SK4558_I2C_SLAVE_ADDR_8bit, reg, val, 1);
    b_printf("%s--> r = %d, reg = %d, val = %d \r", __func__, r, reg, val);
}

uint8_t SK4558_chrg_read(uint8_t reg) 
{
    uint8_t resu;
    u8 r = IIC_ReadSlave(SK4558_I2C_SLAVE_ADDR_8bit, reg & 0xFF, &resu, 1);//i2c function must set as repeat start mode
    b_printf("%s--> r = %d, reg = %d, resu = %d \r", __func__, r, reg, resu);
	return resu;
}
#else
//---------------------------------------------------------------------------
#define SOFT_I2C_OPWR    0
#define SOFT_I2C_OPRD    1

void SK4558_set(void)
{
    SK4558_chrg_constant_voltage(4400);
    SK4558_chrg_precharge_voltage(3000);
    SK4558_chrg_constant_current(190); //1k 380ma
    SK4558_chrg_trickle_current(trickle_current_percent_40);  //80ma
    SK4558_chrg_termination_current(termination_current_percent_10); // 20ma
    SK4558_chrg_truly_shut_off(TRUE);

//------------------------------------------------------------------------------------------------------
    //for test
    // r_printf("====================================================");
    // u8 tt = 0;
    // tt = SK4558_chrg_read(CHRG_REG_SET_CHRG_VOLT);
    // uint8_t vfloat = SK4558_chrg_read(CHRG_REG_SET_CHRG_VOLT) & CHRG_REG_SET_CHRG_VOLT_FLOAT;
    // uint8_t vpre = SK4558_chrg_read(CHRG_REG_SET_CHRG_VOLT) & CHRG_REG_SET_CHRG_VOLT_PRE_CHRG;
    // g_printf("%s ==========> CHRG_VOLT = %02X, vfloat = %02X, vpre = %02X  \r", __func__, tt, vfloat, vpre);

    // tt = SK4558_chrg_read(CHRG_REG_SET_CHRG_CURR);
    // uint8_t Iconst = SK4558_chrg_read(CHRG_REG_SET_CHRG_CURR) & CHRG_REG_SET_CHRG_CURR_CONST;
    // uint8_t Itrickle = SK4558_chrg_read(CHRG_REG_SET_CHRG_CURR) & CHRG_REG_SET_CHRG_CURR_TRICKLE;
    // g_printf("%s ==========> CHRG_CURR = %02X, Iconst = %02X, Itrickle = %02X  \r", __func__, tt, Iconst, Itrickle);
    
    // tt = SK4558_chrg_read(CHRG_REG_SET_CHRG_TERM);
    // uint8_t termCurr = SK4558_chrg_read(CHRG_REG_SET_CHRG_TERM) & CHRG_REG_SET_CHRG_TERM_CURR;
    // uint8_t term_shut = SK4558_chrg_read(CHRG_REG_SET_CHRG_TERM) & CHRG_REG_SET_CHRG_TERM_TRULY_SHUT_OFF;
    // g_printf("%s ==========> CHRG_TERM = %02X, termCurr = %02X, term_shut = %02X  \r", __func__, tt, termCurr, term_shut);
}

void SK4558_init(void)
{
    int retval = 0;
    struct iic_master_config sk4558_iic_cfg = {
        .role = IIC_MASTER,
        .scl_io = IIC_SCL_IO,
        .sda_io = IIC_SDA_IO,
        .io_mode = PORT_INPUT_FLOATING,      //上拉或浮空，如果外部电路没有焊接上拉电阻需要置上拉
        .hdrive = PORT_DRIVE_STRENGT_2p4mA,    //IO口强驱
        .io_filter = 0,                        //软件iic无滤波器
    };

    retval = iic_init(sk4558_iic, &sk4558_iic_cfg);
    if (retval < 0) {
        g_printf("\n !!!!!! open iic for sk4558 err\n");
        return;
    } else {
        g_printf("\n ###### sk4558 iic open succ\n"); 

        int msg[2];
        msg[0] = (int)SK4558_set;
        msg[1] = 0;
        int ret = os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
        if(ret) {
            printf("%s record taskq post err \n", __func__);
        }     
    }
}

/**************************************************************************
 * FUNCTION
 *  SK4558_iic_read
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
uint8_t SK4558_iic_read(soft_iic_dev num, uint8_t addr_dev, uint8_t addr_reg, uint8_t *buf, uint16_t count)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
    uint8_t ret;
    uint8_t ackflag;
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    soft_iic_start(num);
    soft_i2c_select_dev(num, addr_dev, SOFT_I2C_OPWR);
    soft_i2c_writebyte(num, addr_reg);
    soft_iic_start(num);
    soft_i2c_select_dev(num, addr_dev, SOFT_I2C_OPRD);

    for(uint16_t i = 0; i < count; i ++) {
        ackflag = (i < (count-1)) ? 1:0;
        buf[i] = soft_i2c_readbyte(num, ackflag);
    }

    soft_iic_stop(num);
    return ret;
}

/**************************************************************************
 * FUNCTION
 *  SK4558_iic_write
 * DESCRIPTION
 *  
 * PARAMETERS
 *  null
 * RETURNS
 *  null
**************************************************************************/
void SK4558_iic_write(soft_iic_dev num, uint8_t addr_dev, uint8_t addr_reg, uint8_t *buf, uint16_t count)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
 
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    soft_iic_start(num);
    soft_i2c_select_dev(num, addr_dev, SOFT_I2C_OPWR);
    soft_i2c_writebyte(num, addr_reg);

    for (uint16_t i = 0; i < count; i ++) 
    {
        soft_i2c_writebyte(num, buf[i]);
    }

    soft_iic_stop(num);
}

//Communication
void SK4558_chrg_write(uint8_t reg, uint8_t val) {
    // uint8_t data[] = {reg & 0xFF, val & 0xFF};
    SK4558_iic_write(sk4558_iic, SK4558_I2C_SLAVE_ADDR_8bit, reg, &val, 1);
    // b_printf("%s --> reg = %02X, val = %02X \r", __func__, reg, val);
}

uint8_t SK4558_chrg_read(uint8_t reg) {
    uint8_t resu;
    u8 r = SK4558_iic_read(sk4558_iic, SK4558_I2C_SLAVE_ADDR_8bit, reg, &resu, 1);
    // b_printf("%s --> r = %d, reg = %02X, val = %02X \r", __func__, r, reg, resu);
	return resu;
}

#endif

#endif
//------------------------------------------------------------------------------

//Voltage Set : Reg 0x10
/**
 * @brief Regulates Battery Floating Voltage（default: 4200mV）
 *      max 4750
 *      min 4000
 *      step 50
 * 
 * @param  millivolt
 */
void SK4558_chrg_constant_voltage(uint16_t millivolt) {
    uint8_t reg = SK4558_chrg_read(CHRG_REG_SET_CHRG_VOLT) & ~CHRG_REG_SET_CHRG_VOLT_FLOAT;

    if (millivolt > 4750 || millivolt < 4000)
        return;
    uint8_t fifty_millivolt = (millivolt - 4000) / 50; //offset: 4000mV; gear: 50mV;
    
    uint8_t gear = 0;
    if (fifty_millivolt >= 8){gear |= 1<<3;fifty_millivolt -= 8;}
    if (fifty_millivolt >= 4){gear |= 1<<2;fifty_millivolt -= 4;}
    if (fifty_millivolt >= 2){gear |= 1<<1;fifty_millivolt -= 2;}
    if (fifty_millivolt >= 1){gear |= 1<<0;fifty_millivolt -= 1;}        
    
    reg = reg | gear << CHRG_REG_SET_CHRG_VOLT_FLOAT_POS;
    SK4558_chrg_write(CHRG_REG_SET_CHRG_VOLT, reg);

    g_printf("%s success, set Battery Floating Voltage = %d, reg = %02X \r", __func__, millivolt, reg);
}

/**
 * @brief Regulates Battery Precharge Threshold Voltage（default: 2400mV）
 *      max 3000
 *      min 2400
 *      step 100
 * 
 * @param  millivolt
 */
void SK4558_chrg_precharge_voltage(uint16_t millivolt) {
    if (millivolt > 3000 || millivolt < 2400)
        return;
    uint8_t reg = SK4558_chrg_read(CHRG_REG_SET_CHRG_VOLT) & ~CHRG_REG_SET_CHRG_VOLT_PRE_CHRG;
 
    uint8_t hundred_millivolt = (millivolt - 2400) / 100;//offset: 2400mV; gear: 100mV;
    
    uint8_t gear = 0;
    if (hundred_millivolt >= 4){gear |= 1<<2; hundred_millivolt -= 4;}
    if (hundred_millivolt >= 2){gear |= 1<<1; hundred_millivolt -= 2;}
    if (hundred_millivolt >= 1){gear |= 1<<0; hundred_millivolt -= 1;}   

    reg = reg | gear << CHRG_REG_SET_CHRG_VOLT_PRE_CHRG_POS;
    SK4558_chrg_write(CHRG_REG_SET_CHRG_VOLT, reg);

    g_printf("%s success, set Battery Precharge Threshold Voltage = %d, reg = %02X \r", __func__, millivolt, reg);
}

//Current Set : Reg 0x11
/**
 * @brief Regulates Constant Charge Current (CC Current) (%)（default: 10）
 *      max 300 (%)
 *      min 10  (%)
 *      step 10 (%)
 * 
 * @param percent 
 */
void SK4558_chrg_constant_current(uint16_t percent) {
    if (percent > 300 || percent < 10)
        return;

    uint8_t reg = SK4558_chrg_read(CHRG_REG_SET_CHRG_CURR) & ~CHRG_REG_SET_CHRG_CURR_CONST;
    uint8_t ten_percent = (percent - 10) / 10;//offset: 10%; gear: 10%;

    uint8_t gear = 0;
    if (ten_percent >= 16){gear |= 1<<4; ten_percent -= 16;}
    if (ten_percent >= 8){ gear |= 1<<3; ten_percent -= 8;}
    if (ten_percent >= 4){ gear |= 1<<2; ten_percent -= 4;}
    if (ten_percent >= 2){ gear |= 1<<1; ten_percent -= 2;}
    if (ten_percent >= 1){ gear |= 1<<0; ten_percent -= 1;}

    reg = reg | gear << CHRG_REG_SET_CHRG_CURR_CONST_POS;
    SK4558_chrg_write(CHRG_REG_SET_CHRG_CURR, reg);

    g_printf("%s success, set Constant Charge Current percent = %d, gear = %02X, reg = %02X \r", __func__, percent, gear, reg);
}

/**
 * @brief Regulates Trickle Charge Current（default: 20% ; Base: CC Current)
 *      0b000 5%
 *      0b001 10%
 *      0b010 20%
 *      0b100 40%
 * 
 * @param percent 
 */
void SK4558_chrg_trickle_current(chrg_trickle_charge_current_percent percent) {
    uint8_t reg = SK4558_chrg_read(CHRG_REG_SET_CHRG_CURR) & ~CHRG_REG_SET_CHRG_CURR_TRICKLE;

    switch (percent){
    case trickle_current_percent_40:
        reg |= CHRG_REG_SET_CHRG_CURR_TRICKLE_40_PERCENT;
        break;
    case trickle_current_percent_20:
        reg |= CHRG_REG_SET_CHRG_CURR_TRICKLE_20_PERCENT;
        break;
    case trickle_current_percent_10:
        reg |= CHRG_REG_SET_CHRG_CURR_TRICKLE_10_PERCENT;
        break;
    case trickle_current_percent_5:
        reg |= CHRG_REG_SET_CHRG_CURR_TRICKLE_5_PERCENT;
        break;  
    default:
        break;
    }

    SK4558_chrg_write(CHRG_REG_SET_CHRG_CURR, reg);

    g_printf("%s success, set Trickle Charge Current percent = %d, reg = %02X \r", __func__, percent, reg);
}

//Termination Set : Reg 0x12
/**
 * @brief Regulates Termination Current (FOC Current)（default: 10%; Base: CC Current）
 *      0b00 10%
 *      0b01 5%
 *      0b10 2.5%
 * 
 * @param percent 
 */
void SK4558_chrg_termination_current(chrg_termination_charge_current_percent percent) {
    uint8_t reg = SK4558_chrg_read(CHRG_REG_SET_CHRG_TERM) & ~CHRG_REG_SET_CHRG_TERM_CURR;

    switch (percent){
    case termination_current_percent_2p5:
        reg = CHRG_REG_SET_CHRG_TERM_CURR_2p5_PERCENT << CHRG_REG_SET_CHRG_TERM_CURR_POS;
        break;
    case termination_current_percent_5:
        reg = CHRG_REG_SET_CHRG_TERM_CURR_5_PERCENT << CHRG_REG_SET_CHRG_TERM_CURR_POS;
        break;
    case termination_current_percent_10:
        reg = CHRG_REG_SET_CHRG_TERM_CURR_10_PERCENT << CHRG_REG_SET_CHRG_TERM_CURR_POS;
        break;  
    default:
        break;
    }

    SK4558_chrg_write(CHRG_REG_SET_CHRG_TERM, reg);

    g_printf("%s success, set Termination Current (FOC Current) percent = %d, reg = %02X \r", __func__, percent, reg);
}

/**
 * @brief CHG_OFF（default: TRUE）
 *      TRUE shut-off BAT
 *      FALSE don't shut-off BAT
 * 
 * @param enable 
 */
void SK4558_chrg_truly_shut_off(bool enable) {
    uint8_t reg = SK4558_chrg_read(CHRG_REG_SET_CHRG_TERM) & ~CHRG_REG_SET_CHRG_TERM_TRULY_SHUT_OFF;
    g_printf("%s success, set shut-off BAT: enable = %d, reg = %02X \r", __func__, enable, reg);

    if (enable)
        SK4558_chrg_write(CHRG_REG_SET_CHRG_TERM, reg | CHRG_REG_SET_CHRG_TERM_TRULY_SHUT_OFF);
    else
        SK4558_chrg_write(CHRG_REG_SET_CHRG_TERM, reg);

}

#endif

void charge_onoff(bool enable)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
 
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("====== %s : enable = %s \n", __func__, enable?"ENABLE":"DISABLE");

    if(enable){
        gpio_set_mode(IO_PORT_SPILT(CHARGE_ENABLE_CTRL_IO), PORT_HIGHZ);
    }else{
        //disable.
        gpio_set_mode(IO_PORT_SPILT(CHARGE_ENABLE_CTRL_IO), PORT_OUTPUT_HIGH);
    }
}


#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_01)
/**************************************************************************
 * function: charge_check_is_full
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
bool charge_check_is_full(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int d = gpio_read(CHARGE_FULL_CHECK_IO);
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(d == 1){
        mdelay(10);
        d = gpio_read(CHARGE_FULL_CHECK_IO);
        if(d == 1){
            return TRUE;
        }else{
            return FALSE;
        }
    }else{
        return FALSE;
    }
}
#else
/**************************************************************************
 * function: charge_check_is_full
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
bool charge_check_is_full(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
#if (RDX_BJ_VERSION != BJ_BOARD_VERSION_03)
    usb_iomode(1);
#endif
    gpio_set_mode(IO_PORT_SPILT(CHARGE_FULL_CHECK_IO), PORT_INPUT_PULLUP_100K);
    int d = gpio_read(CHARGE_FULL_CHECK_IO);
    if(d == 0){
        mdelay(10);
        d = gpio_read(CHARGE_FULL_CHECK_IO);
        if(d == 0){
            return TRUE;
        }else{
            return FALSE;
        }
    }else{
        return FALSE;
    }
}

#endif

/**************************************************************************
 * function: charge_task_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void charge_task_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables												  */
    /*----------------------------------------------------------------*/
 
    /*----------------------------------------------------------------*/
    /* Code Body													  */
    /*----------------------------------------------------------------*/
    y_printf("====== %s \n", __func__);

#if (RDX_BJ_VERSION == BJ_BOARD_VERSION_01)
    //enable pin.
	struct gpio_config gpio_config_charge_en = {
		.pin = CHARGE_ENABLE_CTRL_PIN,
		.mode = PORT_INPUT_FLOATING,
		.hd = PORT_DRIVE_STRENGT_2p4mA,
	};
	gpio_init(CHARGE_ENABLE_CTRL_PORT, &gpio_config_charge_en);

    charge_onoff(TRUE);

    //check FOC pin.
    gpio_set_mode(IO_PORT_SPILT(CHARGE_FULL_CHECK_IO), PORT_INPUT_PULLUP_1M);

    //iic init.
    SK4558_init();

#elif (RDX_BJ_VERSION == BJ_BOARD_VERSION_02) || (RDX_BJ_VERSION == BJ_BOARD_VERSION_03)
    // usb_iomode(1);
    gpio_set_mode(IO_PORT_SPILT(CHARGE_FULL_CHECK_IO), PORT_INPUT_PULLUP_100K);
#endif
}
