/**
* 注意点：
* 0.此文件变化，在工具端会自动同步修改到工具配置中
* 1.功能块通过【---------xxx------】和 【#endif // xxx 】，是工具识别的关键位置，请勿随意改动
* 2.目前工具暂不支持非文件已有的C语言语法，此文件应使用文件内已有的语法增加业务所需的代码，避免产生不必要的bug
* 3.修改该文件出现工具同步异常或者导出异常时，请先检查文件内语法是否正常
**/


#if TCFG_BATTERY_CURVE_ENABLE
const struct battery_curve g_battery_curve_table [] =  {
    {
        .voltage = 3200,
        .percent = 0
    },
    {
        .voltage = 3600,
        .percent = 5
    },
    {
        .voltage = 3678,
        .percent = 10
    },
    {
        .voltage = 3719,
        .percent = 20
    },
    {
        .voltage = 3755,
        .percent = 30
    },
    {
        .voltage = 3788,
        .percent = 40
    },
    {
        .voltage = 3817,
        .percent = 50
    },
    {
        .voltage = 3866,
        .percent = 60
    },
    {
        .voltage = 3920,
        .percent = 70
    },
    {
        .voltage = 4032,
        .percent = 80
    },
    {
        .voltage = 4117,
        .percent = 90
    },
    {
        .voltage = 4206,
        .percent = 100
    }
};
#endif // TCFG_BATTERY_CURVE_ENABLE

#if TCFG_IO_CFG_AT_POWER_ON
const struct gpio_cfg_item g_io_cfg_at_poweron [] =  {
    {
        .gpio = IO_PORTE_05,
        .mode = PORT_OUTPUT_HIGH,
        .hd = PORT_DRIVE_STRENGT_8p0mA
    }
};
#endif // TCFG_IO_CFG_AT_POWER_ON

#if TCFG_IO_CFG_AT_POWER_OFF
const struct gpio_cfg_item g_io_cfg_at_poweroff [] =  {
    {
        .gpio = IO_PORTE_05,
        .mode = PORT_OUTPUT_LOW,
        .hd = PORT_DRIVE_STRENGT_8p0mA
    }
};
#endif // TCFG_IO_CFG_AT_POWER_OFF

#if TCFG_IOKEY_ENABLE
const struct iokey_info g_iokey_info [] =  {
    {
        .key_value = KEY_IO_NUM0,
        .key_io = IO_PORTG_07,
        .detect = 0,
        .long_press_reset_enable = 0,
        .long_press_reset_time = 16
    },
    {
        .key_value = KEY_IO_NUM1,
        .key_io = IO_PORTB_02,
        .detect = 0,
        .long_press_reset_enable = 0,
        .long_press_reset_time = 8
    },
    {
        .key_value = KEY_IO_NUM2,
        .key_io = IO_PORTG_08,
        .detect = 0,
        .long_press_reset_enable = 0,
        .long_press_reset_time = 8
    },
    {
        .key_value = KEY_IO_NUM3,
        .key_io = IO_PORTB_04,
        .detect = 0,
        .long_press_reset_enable = 0,
        .long_press_reset_time = 8
    },
    {
        .key_value = KEY_IO_NUM4,
        .key_io = IO_PORTC_02,
        .detect = 0,
        .long_press_reset_enable = 0,
        .long_press_reset_time = 8
    }
};
#endif // TCFG_IOKEY_ENABLE

#if TCFG_ADKEY_ENABLE
const struct adkey_res_value adkey_res_table [] =  {
    {
        .key_value = KEY_AD_NUM0,
        .res_value = 0
    },
    {
        .key_value = KEY_AD_NUM1,
        .res_value = 62
    },
    {
        .key_value = KEY_AD_NUM2,
        .res_value = 150
    },
    {
        .key_value = KEY_AD_NUM3,
        .res_value = 330
    }
};

const struct adkey_info g_adkey_data =  {
    .key_io = IO_PORTB_01,
    .pull_up_type = 1,
    .pull_up_value = 220,
    .max_ad_value = 1023,
    .long_press_reset_enable = 0,
    .long_press_reset_time = 8,
    .res_table = adkey_res_table
};
#endif // TCFG_ADKEY_ENABLE

#if TCFG_LP_TOUCH_KEY_ENABLE
const struct touch_key_cfg lp_touch_key_table [] =  {
    {
        .key_ch = LPCTMU_CH1_PB1,
        .key_value = KEY_POWER,
        .wakeup_enable = 1,
        .eartch_en = 0,
        .index = 0,
        .algo_cfg = {
            {
                .algo_cfg0 = 20,
                .algo_cfg1 = 25,
                .algo_cfg2 = 80,
                .algo_range_min = 50,
                .algo_range_max = 500,
                .range_sensity = 6
            }
        }
    }
};
#endif // TCFG_LP_TOUCH_KEY_ENABLE
