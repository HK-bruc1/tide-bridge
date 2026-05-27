#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".board_config.data.bss")
#pragma data_seg(".board_config.data")
#pragma const_seg(".board_config.text.const")
#pragma code_seg(".board_config.text")
#endif
#include "asm/power_interface.h"
#include "gpadc.h"
#include "app_config.h"
#include "gSensor/gSensor_manage.h"
#include "imuSensor_manage.h"
#include "iic_api.h"
#include "audio_adc.h"
#include "asm/power_interface.h"
#include "eartouch_manage.h"

#if TCFG_MPU6887P_ENABLE
#include "imu_sensor/mpu6887/mpu6887p.h"
#endif /*TCFG_MPU6887P_ENABLE*/
#if TCFG_QMI8658_ENABLE
#include "imu_sensor/qmi8658/qmi8658c.h"
#endif /*TCFG_QMI8658_ENABLE*/
#if TCFG_LSM6DSL_ENABLE
#include "imu_sensor/lsm6dsl/lsm6dsl.h"
#endif /*TCFG_LSM6DSL_ENABLE*/
#if TCFG_ICM42670P_ENABLE
#include "imu_sensor/icm_42670p/icm_42670p.h"
#endif /*TCFG_ICM42670P_ENABLE*/

const struct iic_master_config soft_iic_cfg_const[MAX_SOFT_IIC_NUM] = {
    //soft iic0
    {
        .role = IIC_MASTER,
        .scl_io = TCFG_SW_I2C0_CLK_PORT,
        .sda_io = TCFG_SW_I2C0_DAT_PORT,
        .io_mode = PORT_INPUT_PULLUP_10K,      //上拉或浮空，如果外部电路没有焊接上拉电阻需要置上拉
        .hdrive = PORT_DRIVE_STRENGT_2p4mA,    //IO口强驱
        .master_frequency = TCFG_SW_I2C0_DELAY_CNT,  //IIC通讯波特率 未使用
        .io_filter = 0,                        //软件iic无滤波器
    },
#if 0
    //soft iic1
    {
        .role = IIC_MASTER,
        .scl_io = TCFG_SW_I2C1_CLK_PORT,
        .sda_io = TCFG_SW_I2C1_DAT_PORT,
        .io_mode = PORT_INPUT_PULLUP_10K,      //上拉或浮空，如果外部电路没有焊接上拉电阻需要置上拉
        .hdrive = PORT_DRIVE_STRENGT_2p4mA,    //IO口强驱
        .master_frequency = TCFG_SW_I2C1_DELAY_CNT,  //IIC通讯波特率 未使用
        .io_filter = 0,                        //软件iic无滤波器
    },

#endif
};

const struct iic_master_config hw_iic_cfg_const[MAX_HW_IIC_NUM] = {
    {
        .role = IIC_MASTER,
#if (defined CONFIG_CPU_BR36 || defined CONFIG_CPU_BR27)
        .scl_io = TCFG_HW_I2C0_CLK_PORT,
        .sda_io = TCFG_HW_I2C0_DAT_PORT,
#else
#endif
        .io_mode = PORT_INPUT_PULLUP_10K,      //上拉或浮空，如果外部电路没有焊接上拉电阻需要置上拉
        .hdrive = PORT_DRIVE_STRENGT_2p4mA,    //IO口强驱
        .master_frequency = TCFG_HW_I2C0_CLK,  //IIC通讯波特率
        .io_filter = 1,                        //是否打开滤波器（去纹波）
    },
};

/************************** imu sensor ****************************/
#if TCFG_IMUSENSOR_ENABLE
IMU_SENSOR_PLATFORM_DATA_BEGIN(imu_sensor_data)
#if TCFG_SH3001_ENABLE
{
    .peripheral_hdl = TCFG_SH3001_USER_IIC_INDEX,     //iic_hdl     or  spi_hdl
     .peripheral_param0 = 0,  //iic_delay(iic byte间间隔)   or spi_cs_pin
      .imu_sensor_name = "sh3001",
       .imu_sensor_int_io = TCFG_SH3001_DETECT_IO,
},
#endif
#if TCFG_TP_MPU9250_ENABLE
{
    .peripheral_hdl = TCFG_MPU9250_USER_IIC_INDEX,     //iic_hdl     or  spi_hdl
    .peripheral_param0 = 1, //iic_delay(iic byte间间隔)   or spi_cs_pin//iic byte间间隔
    .imu_sensor_name = "mpu9250",
    .imu_sensor_int_io = TCFG_MPU9250_DETECT_IO,
},
#endif
#if TCFG_MPU6887P_ENABLE
{
    .peripheral_hdl = TCFG_MPU6887P_USER_IIC_INDEX,     //iic_hdl     or  spi_hdl
    .peripheral_param0 = 0, //iic_delay(iic byte间间隔)   or spi_cs_pin//iic byte间间隔
    .imu_sensor_name = "mpu6887p",
    .imu_sensor_int_io = TCFG_MPU6887P_DETECT_IO,
},
#endif
#if TCFG_ICM42670P_ENABLE
{
    .peripheral_hdl = TCFG_ICM42670P_USER_IIC_INDEX,     //iic_hdl     or  spi_hdl
    .peripheral_param0 = 0, //iic_delay(iic byte间间隔)   or spi_cs_pin//iic byte间间隔
    .imu_sensor_name = "icm42670p",
    .imu_sensor_int_io = TCFG_ICM42670P_DETECT_IO,
},
#endif
#if TCFG_MPU6050_EN //未整合参数如GSENSOR_PLATFORM_DATA_BEGIN配置
{
    .peripheral_hdl = 0,     //iic_hdl     or  spi_hdl
    .peripheral_param0 = 0, //iic_delay(iic byte间间隔)   or spi_cs_pin//iic byte间间隔
    .imu_sensor_name = "mpu6050",
    .imu_sensor_int_io = -1,
},
#endif
#if TCFG_QMI8658_ENABLE
{
    .peripheral_hdl = TCFG_QMI8658_USER_IIC_INDEX,     //iic_hdl     or  spi_hdl
    .peripheral_param0 = 0, //iic_delay(iic byte间间隔)   or spi_cs_pin//iic byte间间隔
    .imu_sensor_name = "qmi8658",
    .imu_sensor_int_io = TCFG_QMI8658_DETECT_IO,
},
#endif
#if TCFG_LSM6DSL_ENABLE
{
    .peripheral_hdl = TCFG_LSM6DSL_USER_IIC_INDEX,     //iic_hdl     or  spi_hdl
    .peripheral_param0 = 0, //iic_delay(iic byte间间隔)   or spi_cs_pin//iic byte间间隔
    .imu_sensor_name = "lsm6dsl",
    .imu_sensor_int_io = TCFG_LSM6DSL_DETECT_IO,
},
#endif
IMU_SENSOR_PLATFORM_DATA_END();
#endif

#if TCFG_OUTSIDE_EARTCH_ENABLE
eartouch_platform_data eartouch_cfg = {
#if TCFG_EARTCH_SENSOR_SEL == EARTCH_OUTSDIE_TCH
    .logo = "outside_tch",
#elif TCFG_EARTCH_SENSOR_SEL == EARTCH_HX300X
    .logo = "hx300x",
#endif
    .pwr_port = TCFG_EARTCH_PWR_PORT,
    .int_port = TCFG_EARTCH_INT_PORT,
#if (TCFG_EARTCH_SENSOR_SEL  !=  EARTCH_OUTSDIE_TCH)
    .iic_clk  = TCFG_EARTCH_CLK_PORT,
    .iic_dat  = TCFG_EARTCH_DAT_PORT,
#endif
    .in_filter_cnt = 2, 			    //戴上消抖
    .out_filter_cnt = 3,   		        //拿下消抖
};
#endif

