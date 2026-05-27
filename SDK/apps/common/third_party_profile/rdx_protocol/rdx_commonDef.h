/*=====================================================================================
 HEADER NAME: rdx_commonDef.h
 MODULE NAME: rdx common define headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-10-16 22:51:17
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 22:51:25
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_commonDef.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_COMMONDEF_H__
#define __RDX_COMMONDEF_H__

/******************************************************************************
* Include files
******************************************************************************/ 


/******************************************************************************
* Macro Define Section
******************************************************************************/ 
typedef unsigned char         WE_BOOL  ;      /*BOOLEAN : unsigned char(1/0)        prefix: b     */
typedef char                  WE_CHAR  ;      /*CHAR    : char                      prefix: c     */
typedef unsigned char         WE_UCHAR;      /*UCHAR   : unsigned char             prefix: uc    */
typedef char                  WE_INT8  ;      /*INT8    : 8 bit  integer            prefix: c     */
typedef short				  WE_INT16;      /*INT16   : 16 bit integer            prefix: s     */
typedef unsigned short        WE_UINT16;      /*UINT16  : 16 bit unsigned integer   prefix: us    */
typedef int                   WE_INT  ;      /*INT     : general integer           prefix: i     */
typedef signed int            WE_SINT  ;      /*SINT    : general signed integer    prefix: si    */
typedef unsigned int          WE_UINT  ;      /*UINT    : general unsigned integer  prefix: ui    */
typedef int                   WE_INT32;      /*INT32   : 32 bit integer            prefix: i     */
typedef unsigned int          WE_UINT32;      /*UINT32  : 32 bit unsigned integer   prefix: ui    */
typedef long                  WE_LONG  ;      /*LONG    : long                      prefix: l     */
typedef signed long           WE_SLONG;      /*SLONG   : signed long               prefix: sl    */     
typedef unsigned long         WE_ULONG;      /*ULONG   : unsigned long             prefix: ul    */ 
typedef float                 WE_FLOAT;      /*FLOAT   : float                     prefix: f     */               
typedef double                WE_DOUBLE;      /*DOUBLE  : double float              prefix: d     */  
typedef void                  WE_VOID  ;      /*VOID    : void                      prefix: v     */       
typedef void *                WE_HANDLE;      /*HANDLE  : for some object's pointer prefix: h     */


//untility.
#define PROTOCOL_CLASS(X) typedef struct  tag##X X; struct tag##X

/* the type of an interface's method vtable */
#define AEEVTBL(iname) iname##Vtbl

/* shortcut for declaring an abstract interface */
#define AEEINTERFACE(iname) \
           typedef struct AEEVTBL(iname) AEEVTBL(iname); \
           struct AEEVTBL(iname)

/* macro for retrieving the vtable from an instance of an interface */
#define AEEGETPVTBL(p,iname)  (*((AEEVTBL(iname) **)((void *)p)))

/* Define an AEE interface vtable (Assumes INHERIT_iname exists for iname) */
#define AEEVTBL_DEFINE(iname)\
            AEEINTERFACE(iname) {\
               INHERIT_##iname(iname);\
            }

/* Define an AEE interface (Assumes INHERIT_iname exists for iname) */
#define AEEINTERFACE_DEFINE(iname)\
            typedef struct iname iname;\
            AEEVTBL_DEFINE(iname)


/*Define Constant Macro start*/
#ifndef NULL
#define NULL                                (void *)0 /* NULL  :  Null pointer    */
#endif

#ifndef TRUE
#define TRUE                                1       /* TRUE  :  Integer value 1 */
#endif

#ifndef FALSE
#define FALSE                               0       /* FALSE :  Integer value 0 */
#endif

#ifndef MALLOC
#define MALLOC(size)                        malloc(size)
#endif

#ifndef FREE
#define FREE(data)                          free(data)
#endif

#ifndef REALLOC
#define REALLOC(data, size)                 realloc(data, size)
#endif

#ifndef FREEIF
#define FREEIF(p)                           do{if(p) {FREE(p);p = NULL;}}while(0)
#endif

#ifndef MEMSET
#define MEMSET(dest, value, len)			memset(dest, value, len)
#endif

#ifndef MEMCPY
#define MEMCPY(dst, src, len)				memcpy(dst, src, len)
#endif



#ifndef BV
#define BV(n)                               (1 << (n))
#endif

#ifndef BF
#define BF(x,b,s)                           (((x) & (b)) >> (s))
#endif

#ifndef MIN
#define MIN(n,m)                            (((n) < (m)) ? (n) : (m))
#endif

#ifndef MAX
#define MAX(n,m)                            (((n) < (m)) ? (m) : (n))
#endif

#ifndef ABS
#define ABS(n)                              (((n) < 0) ? -(n) : (n))
#endif


#define GET_HIGHBYTE(data)					((data & 0xff00)>>8)
#define GET_LOWBYTE(data)					(data & 0xff)
#define COMBINE_BYTES(high,low)				((high << 8) + low)  
	
#define GET_INT_BYTE1(data)					(data & 0xff)	
#define GET_INT_BYTE2(data)					((data & 0xff00)>>8)
#define GET_INT_BYTE3(data)					((data & 0xff0000)>>16)
#define GET_INT_BYTE4(data)					((data & 0xff000000)>>24)
#define COMBINE_INT_BYTES(b4,b3,b2,b1)		((b4 << 24) + (b3 << 16) + (b2 << 8) + b1)  

#define ASSIGN_STRING(tar,src)            do{tar = src;src = NULL;}while(0)
#define ASSIGN_INT(tar,src)               do{if(src) {tar = atoi(src);}}while(0)
#define ASSIGN_LONG_INT(tar,src)          do{if(src) {tar = atol(src);}}while(0)


/*******************************************************************************
* Structure and Enum Section
*******************************************************************************/



#endif
