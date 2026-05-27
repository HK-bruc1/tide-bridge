
#ifndef SK4558_H
#define SK4558_H

#define SK4558_I2C_SLAVE_ADDR_7bit                  0x17
#define SK4558_I2C_SLAVE_ADDR_8bit                  0x2E

#define CHRG_REG_SET_CHRG_VOLT                      0x10
#define CHRG_REG_SET_CHRG_VOLT_FLOAT                (0xf<<4)
#define CHRG_REG_SET_CHRG_VOLT_FLOAT_POS            (4)
#define CHRG_REG_SET_CHRG_VOLT_PRE_CHRG             (0x7<<1)
#define CHRG_REG_SET_CHRG_VOLT_PRE_CHRG_POS         (1)

#define CHRG_REG_SET_CHRG_CURR                      0x11
#define CHRG_REG_SET_CHRG_CURR_CONST                (0x1f<<3)
#define CHRG_REG_SET_CHRG_CURR_CONST_POS            (3)
/* three-bit field, valid values below */
#define CHRG_REG_SET_CHRG_CURR_TRICKLE              (0x7<<0)
#define CHRG_REG_SET_CHRG_CURR_TRICKLE_40_PERCENT   (0x4)
#define CHRG_REG_SET_CHRG_CURR_TRICKLE_20_PERCENT   (0x2)
#define CHRG_REG_SET_CHRG_CURR_TRICKLE_10_PERCENT   (0x1)
#define CHRG_REG_SET_CHRG_CURR_TRICKLE_5_PERCENT    (0x0)
#define CHRG_REG_SET_CHRG_CURR_TRICKLE_POS          (0)

#define CHRG_REG_SET_CHRG_TERM                      0x12
/* two-bit field, valid values below */
#define CHRG_REG_SET_CHRG_TERM_CURR                 (0x3<<6)
#define CHRG_REG_SET_CHRG_TERM_CURR_2p5_PERCENT     (0x2)
#define CHRG_REG_SET_CHRG_TERM_CURR_5_PERCENT       (0x1)             
#define CHRG_REG_SET_CHRG_TERM_CURR_10_PERCENT      (0x0)
#define CHRG_REG_SET_CHRG_TERM_CURR_POS             (6)
#define CHRG_REG_SET_CHRG_TERM_TRULY_SHUT_OFF       (1<<5)

typedef enum{
    trickle_current_percent_5,          // 5%
    trickle_current_percent_10,         // 10%
    trickle_current_percent_20,         // 20%
    trickle_current_percent_40,         // 40%
}chrg_trickle_charge_current_percent;

typedef enum{
    termination_current_percent_2p5,    // 2.5%
    termination_current_percent_5,      // 5%
    termination_current_percent_10,     // 10%
}chrg_termination_charge_current_percent;

//Voltage Set
void SK4558_chrg_constant_voltage(uint16_t millivolt);
void SK4558_chrg_precharge_voltage(uint16_t millivolt);

//Current Set
void SK4558_chrg_constant_current(uint16_t percent);
void SK4558_chrg_trickle_current(chrg_trickle_charge_current_percent percent);

//Termination Set
void SK4558_chrg_termination_current(chrg_termination_charge_current_percent percent);
void SK4558_chrg_truly_shut_off(bool enable);

void charge_task_init(void);
void charge_onoff(bool enable);
bool charge_check_is_full(void);

#endif