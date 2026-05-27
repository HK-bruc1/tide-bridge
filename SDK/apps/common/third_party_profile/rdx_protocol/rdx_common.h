/*=====================================================================================
 HEADER NAME: rdx_common.h
 MODULE NAME: rdx common application headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-10-16 22:50:27
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 22:50:30
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_common.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_COMMON_H__
#define __RDX_COMMON_H__

/******************************************************************************
* Include files
******************************************************************************/ 

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define DS_DEBUG

#ifndef DS_TRACE
	#ifdef DS_DEBUG
		#define DS_TRACE(fmt, ...)				y_printf(fmt, ##__VA_ARGS__);
	#else
		#define DS_TRACE(fmt, ...) 
	#endif

#endif

#ifndef DS_ASSERT
	#ifdef DS_FUNCTION_DEBUG
	#define DS_ASSERT(ret,info)        			while(!(ret)){DS_TRACE(info);} 
	#else
	#define DS_ASSERT(ret,info)	
	#endif
#endif

#ifndef STRDUP
#define STRDUP(s)        						rdx_strdup(s)
#endif

#define DS_TRACE_ERROR()		  		  		DS_TRACE("?????????????????????ERROR????????????????????????????")

#define BREAK_ERROR(c)							if(E_PROTOCOL_ECODE_SUCCESS != c) EXCEPTION_THROW()

/* catch exception. */
#define EXCEPTION_THROW()                       goto _PROTOCOL_EXCEPTION
#define EXCEPTION_POINTER()                     _PROTOCOL_EXCEPTION:


#define RDX_MAC_LEN								(6)

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 


#endif