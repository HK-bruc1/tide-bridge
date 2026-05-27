/*=====================================================================================
 HEADER NAME: rdx_util.h
 MODULE NAME: rdx util module headfile.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2024-09-24 20:34:31
 LastEditors: sheng.dong
 LastEditTime: 2024-10-16 14:12:50
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_util.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_UTIL_H__
#define __RDX_UTIL_H__

/******************************************************************************
* Include files
******************************************************************************/ 
#include "app_msg.h"


#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * @brief rdx_util_check_sum8
 *
 * @param[in] buf: buf
 * @param[in] size: size
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u8 rdx_util_check_sum8(u8* buf, u32 size);

/**
 * @brief rdx_util_check_sum16
 *
 * @param[in] buf: buf
 * @param[in] size: size
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u16 rdx_util_check_sum16(u8* buf, u32 size);

/**
 * @brief rdx_util_crc16
 *
 * @param[in] buf: buf
 * @param[in] size: size
 * @param[in] p_crc: p_crc
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u16 rdx_util_crc16(u8* buf, u32 size, u16* p_crc);

/**
 * @brief rdx_util_crc32
 *
 * @param[in] buf: buf
 * @param[in] size: size
 * @param[in] p_crc: p_crc
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_crc32(u8* buf, u32 size, u32* p_crc);

/**
 * @brief rdx_util_intarray2int
 *
 * @param[in] intArray: intArray
 * @param[in] startIdx: startIdx
 * @param[in] size: size
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_intarray2int(u8* intArray, u32 startIdx, u32 size);

/**
 * @brief rdx_util_int2intarray
 *
 * @param[in] num: num
 * @param[in] intArray: intArray
 * @param[in] size: size
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_int2intarray(u32 num, u8* intArray, u32 size);

/**
 * @brief rdx_util_device_id_20_to_16
 *
 * @param[in] *in: *in
 * @param[out] *out: *out
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
void rdx_util_device_id_20_to_16(u8 *in, u8 *out);

/**
 * @brief rdx_util_device_id_16_to_20
 *
 * @param[in] *in: *in
 * @param[out] *out: *out
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
void rdx_util_device_id_16_to_20(u8 *in, u8 *out);

/**
 * @brief rdx_util_reverse_byte
 *
 * @param[in] buf: void* Prevent warnings when calling this API
 * @param[in] size: size
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_reverse_byte(void* buf, u32 size);

/**
 * @brief rdx_util_count_one_in_num
 *
 * @param[in] num: num
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_count_one_in_num(u32 num);

/**
 * @brief rdx_util_buffer_value_is_all_x
 *
 * @param[in] *buf: *buf
 * @param[in] size: size
 * @param[in] x_value: x_value
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
bool rdx_util_buffer_value_is_all_x(const u8 *buf, u32 size, u8 x_value);

/**
 * @brief rdx_util_is_word_aligned
 *
 * @param[in] p: p
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
bool rdx_util_is_word_aligned(void const* p);

/**
 * @brief rdx_util_search_symbol_index
 *
 * @param[in] *buf: *buf
 * @param[in] size: size
 * @param[in] symbol: symbol
 * @param[in] index[]: index[]
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
int rdx_util_search_symbol_index(u8 *buf, u32 size, u8 symbol, u8 index[]);

/**
 * @brief rdx_util_ecc_key_pem2hex
 *
 * @param[in] *pem: *pem
 * @param[in] *key: *key
 * @param[in] *key_len: *key_len
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
int rdx_util_ecc_key_pem2hex(const u8 *pem, u8 *key, u16 *key_len);

/**
 * @brief rdx_util_ecc_sign_secp256r1_extract_raw_from_der
 *
 * @param[in] *der: *der
 * @param[in] *raw_rs: *raw_rs
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
int rdx_util_ecc_sign_secp256r1_extract_raw_from_der(const u8 *der, u8 *raw_rs);

/**
 * @brief rdx_util_shell_sort
 *
 * @param[in] buf: buf
 * @param[in] size: size
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_shell_sort(int* buf, int size);

/**
 * @brief rdx_util_str_hexchar2int
 *
 * @param[in] hexChar: hexChar
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u8 rdx_util_str_hexchar2int(u8 hexChar);

/**
 * @brief rdx_util_str_int2hexchar
 *
 * @param[in] isHEX: isHEX
 * @param[in] intNum: intNum
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u8 rdx_util_str_int2hexchar(bool isHEX, u8 intNum);

/**
 * @brief rdx_util_str_hexstr2int
 *
 * @param[in] hexStr: hexStr
 * @param[in] size: size
 * @param[in] num: num
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_str_hexstr2int(u8* hexStr, u32 size, u32* num);

/**
 * @brief rdx_util_str_int2hexstr
 *
 * @param[in] isHEX: isHEX
 * @param[in] num: num
 * @param[in] hexStr: hexStr
 * @param[in] size: size
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_str_int2hexstr(bool isHEX, u32 num, u8* hexStr, u32 size);

/**
 * @brief rdx_util_str_intstr2int
 *
 * @param[in] intStr: intStr
 * @param[in] size: size
 * @param[out] num: num
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_str_intstr2int(u8* intStr, u32 size, u32* num);

/**
 * @brief rdx_util_str_intstr2int_with_negative
 *
 * @param[in] intStr: intStr
 * @param[in] size: size
 * @param[out] num: num
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_str_intstr2int_with_negative(char* intStr, u32 size, int* num);

/**
 * @brief rdx_util_str_int2intstr
 *
 * @param[in] num: num
 * @param[in] intStr: intStr
 * @param[in] size: size
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_str_int2intstr(u32 num, u8* intStr, u32 size);

/**
 * @brief rdx_util_str_hexstr2hexarray
 *
 * @param[in] hexStr: hexStr
 * @param[in] size: size
 * @param[in] hexArray: hexArray
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_str_hexstr2hexarray(u8* hexStr, u32 size, u8* hexArray);

/**
 * @brief rdx_util_str_hexarray2hexstr
 *
 * @param[in] isHEX: isHEX
 * @param[in] hexArray: hexArray
 * @param[in] size: size
 * @param[in] hexStr: hexStr
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u32 rdx_util_str_hexarray2hexstr(bool isHEX, u8* hexArray, u32 size, u8* hexStr);

/**
 * @brief rdx_util_get_value_by_key
 *
 * @param[in] *input_buf: *input_buf
 * @param[in] input_len: input_len
 * @param[in] *key: *key
 * @param[in] key_len: key_len
 * @param[in] *value_data: *value_data
 * @param[in] *value_len: *value_len
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u8 rdx_util_get_value_by_key(u8 *input_buf, u16 input_len, u8 *key, u16 key_len, u8 *value_data, u16 *value_len);

/**
 * @brief rdx_util_get_value_by_key_to_int
 *
 * @param[in] *input_buf: *input_buf
 * @param[in] input_len: input_len
 * @param[in] *key: *key
 * @param[in] key_len: key_len
 * @param[in] *result: *result
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u8 rdx_util_get_value_by_key_to_int(u8 *input_buf, u16 input_len, u8 *key, u8 key_len, u32 *result);

/**
 * @brief rdx_util_get_value_by_key_to_hex
 *
 * @param[in] *input_buf: *input_buf
 * @param[in] input_len: input_len
 * @param[in] *key: *key
 * @param[in] key_len: key_len
 * @param[in] *result: *result
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u8 rdx_util_get_value_by_key_to_hex(u8 *input_buf, u16 input_len, u8 *key, u8 key_len, u32 *result);

/**
 * @brief rdx_util_get_value_by_key_to_bool
 *
 * @param[in] *input_buf: *input_buf
 * @param[in] input_len: input_len
 * @param[in] *key: *key
 * @param[in] key_len: key_len
 * @param[in] *result: *result
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
u8 rdx_util_get_value_by_key_to_bool(u8 *input_buf, u16 input_len, u8 *key, u8 key_len, u32 *result);


int rdx_util_hex_to_char(const char* hex, char *out);

int rdx_util_version_str2int(char *buf, u32 len);

u8 rdx_util_base64_encode(const u8 *input, u16 inlen, u8 *output, u16 *outlen);
u8 rdx_util_base64_decode(const u8 *input, u16 inlen, u8 *output, u16 *outlen);


#ifdef __cplusplus
}
#endif

#endif /* __RDX_UTIL_H__ */

