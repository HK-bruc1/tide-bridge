/*=====================================================================================
 HEADER NAME: rdx_common.c
 MODULE NAME: rdx common application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-10-16 22:48:27
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 22:48:31
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_common.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#include "rdx_commonDef.h"
#include "system/includes.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 


/******************************************************************************
* Global Variables Section
******************************************************************************/ 

/******************************************************************************
* Local Variables Section
******************************************************************************/ 

/******************************************************************************
* Function Section
******************************************************************************/ 


/**************************************************************************
 * function: rdx_strdup
 * description: 
 * param (char *) s
 * return (*)
 **************************************************************************/
char* rdx_strdup(char * s)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
	int len = strlen(s);
	char *r = NULL;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
	r = (WE_CHAR *)MALLOC(len + 2);
	if(!r){
		DS_TRACE_ERROR();
		DS_TRACE("DM_strdup:fail to malloc string.");
		return NULL;
	}

	MEMSET(r,0,len + 2);
	MEMCPY(r,s,len);

	return r;
}


//=====================================================================================
void *calloc(unsigned long count, unsigned long size)
{
    void *p;

    p = malloc(count * size);
    if (p) {
        memset(p, 0, count * size);
    }
    return p;
}

void *_calloc_r(struct _reent *r, size_t a, size_t b)
{
    return calloc(a, b);
}

