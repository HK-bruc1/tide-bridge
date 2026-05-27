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
 Date: 2025-09-24 10:06:28
 LastEditors: sheng.dong
 LastEditTime: 2025-09-24 21:26:40
 FilePath: \SDK\interface\driver\device\usb\device\webusb.h
 
 Self-documenting Code
=====================================================================================*/
#ifndef __USBD_WEBUSB_H__
#define __USBD_WEBUSB_H__

#include "usb/usb.h"
#include "usb_stack.h"


/******************************************************************************
* Function Section
******************************************************************************/ 
u32 webusb_desc_config(const usb_dev usb_id, u8 *ptr, u32 *cur_itf_num);
u32 webusb_setup_device_hook(struct usb_device_t *usb_device, struct usb_ctrlrequest *req);
u8 webusb_get_auth_status(void);
int webusb_register(const usb_dev usb_id);
u32 webusb_release(const usb_dev usb_id);
u32 webusb_set_wakeup_handle(void (*handle)(struct usb_device_t *usb_device));

#endif /* __USBD_WEBUSB_H__ */