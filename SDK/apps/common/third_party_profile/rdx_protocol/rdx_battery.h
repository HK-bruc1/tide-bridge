/*=====================================================================================
 HEADER NAME: rdx_battery.h
 MODULE NAME: rdx battery module headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-10-16 22:52:41
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 22:52:42
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_battery.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_BATTERY__
#define __RDX_BATTERY__

/******************************************************************************
* Include files
******************************************************************************/ 
#include "typedef.h"


/******************************************************************************
* Macro Define Section
******************************************************************************/ 


/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
typedef enum{
    BAT_LEVEL_UNKNOWN,
    BAT_LEVEL_0, //0格电
    BAT_LEVEL_1, //1格电
    BAT_LEVEL_2,
    BAT_LEVEL_3,
    BAT_LEVEL_4,
    BAT_LEVEL_5, //5格电
    BAT_LEVEL_6, //满电
}BatLevel;

typedef struct{
    u8 percent;
    BatLevel level;
    BatLevel orig_level;
}BatteryInfo;

/******************************************************************************
* Local Variables Section
******************************************************************************/ 



/******************************************************************************
* Global Variables Section
******************************************************************************/ 


/******************************************************************************
* Function Section
******************************************************************************/ 



#endif