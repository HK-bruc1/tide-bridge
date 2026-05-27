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
 Date: 2025-01-10 23:54:46
 LastEditors: sheng.dong
 LastEditTime: 2025-02-15 23:49:02
 FilePath: \SDK\apps\common\device\motor\motor.h
 
 Self-documenting Code
=====================================================================================*/
#ifndef __MOTOR_H
#define __MOTOR_H 


/******************************************************************************
* Include files
******************************************************************************/ 
#include "stdlib.h"	


/******************************************************************************
* Function Section
******************************************************************************/ 
extern void motor_on(void);
extern void motor_off(void);


#endif