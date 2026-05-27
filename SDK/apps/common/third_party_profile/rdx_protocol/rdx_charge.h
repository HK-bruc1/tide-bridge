/*=====================================================================================
 HEADER NAME: rdx_charge.h
 MODULE NAME: application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-05-08 10:01:35
 LastEditors: sheng.dong
 LastEditTime: 2025-05-08 10:02:09
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_charge.h
 
 Self-documenting Code
=====================================================================================*/


#ifndef _RDX_CHARGE_H_
#define _RDX_CHARGE_H_

/******************************************************************************
* Include files
******************************************************************************/ 
#include "asm/cpu.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define RDX_CHARGE_OUT                                      (0)
#define RDX_CHARGE_IN                                       (1)
#define RDX_CHARGE_FULL                                     (2)

#define RDX_APP_INCHARGE_FULL_CHECK_TIMEOUT                 (5000)
#define RDX_APP_INCHARGE_FULL_POWEROFF_TIMEOUT              (5000)

#define RDX_APP_INCHARGE_BATTERY_SHOW_TIMEOUT				(5000)


/******************************************************************************
* Function Section
******************************************************************************/ 
u8 rdx_app_get_charge_state(void);
void rdx_app_incharge_batPercent_show_stop(void);
void rdx_app_incharge_batPercent_show_start(void);


#endif