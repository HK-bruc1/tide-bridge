/*=====================================================================================
 HEADER NAME: rdx_util.c
 MODULE NAME: rdx util module.
 
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
 LastEditTime: 2024-10-16 13:51:53
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_util.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#include "string.h"
#include "rdx_util.h"

/******************************************************************************
* Macro Define Section
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
 * function: rdx_util_check_sum8
 * description: 
 * param (u8*) buf
 * param (u32) size
 * return (*)
 **************************************************************************/
u8 rdx_util_check_sum8(u8* buf, u32 size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 sum = 0;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (u32 idx=0; idx<size; idx++) {
        sum += buf[idx];
    }
    return sum;
}

/**************************************************************************
 * function: rdx_util_check_sum16
 * description: 
 * param (u8*) buf
 * param (u32) size
 * return (*)
 **************************************************************************/
u16 rdx_util_check_sum16(u8* buf, u32 size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u16 sum = 0;   
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (u32 idx=0; idx<size; idx++) {
        sum += buf[idx];
    }
    return sum;
}

/**************************************************************************
 * function: rdx_util_crc16
 * description: 
 * param (u8*) buf
 * param (u32) size
 * param (u16*) p_crc
 * return (*)
 **************************************************************************/
u16 rdx_util_crc16(u8* buf, u32 size, u16* p_crc)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u16 poly[2] = {0, 0xa001}; //0x8005 ---- 0xa001
    u16 crc;
    int i, j;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    crc = (p_crc == NULL) ? 0xFFFF: *p_crc;

    for (j=size; j>0; j--) {
        u8 ds = *buf++;
        for (i=0; i<8; i++) {
            crc = (crc >> 1) ^ poly[(crc ^ ds) & 1];
            ds = ds >> 1;
        }
    }
    return crc;
}

#if 0

/**************************************************************************
 * function: rdx_util_crc32
 * description: 
 * param (u8*) buf
 * param (u32) size
 * param (u32*) p_crc
 * return (*)
 **************************************************************************/
u32 rdx_util_crc32(u8* buf, u32 size, u32* p_crc)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u32 crc = (p_crc == NULL) ? 0xFFFFFFFF : ~(*p_crc);
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (u32 i = 0; i < size; i++) {
        crc = crc ^ buf[i];
        for (u32 j = 8; j > 0; j--) {
            crc = (crc >> 1) ^ (0xEDB88320U & ((crc & 1) ? 0xFFFFFFFF : 0));
        }
    }
    return ~crc;
}

#else

static u32 crc32_table[256];

// 初始化CRC表（只需执行一次）
void rdx_init_crc32_table() 
{
    for (u32 i = 0; i < 256; i++) {
        u32 crc = i;
        for (u32 j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320U & ((crc & 1) ? 0xFFFFFFFF : 0));
        }
        crc32_table[i] = crc;
    }
}

u32 rdx_util_crc32(u8* buf, u32 size, u32* p_crc) 
{
    u32 crc = (p_crc == NULL) ? 0xFFFFFFFF : ~(*p_crc);
    
    for (u32 i = 0; i < size; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc & 0xFF) ^ buf[i]];
    }
    
    return ~crc;
}

#endif

/**************************************************************************
 * function: rdx_util_intarray2int
 * description: 
 * param (u8*) intArray
 * param (u32) startIdx
 * param (u32) size
 * return (*)
 **************************************************************************/
u32 rdx_util_intarray2int(u8* intArray, u32 startIdx, u32 size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (startIdx >= size) {
        return (u32)-1;
    }

    u32 num = 0;
    for (u32 idx=startIdx; idx<startIdx+size; idx++) {
        num = (num*10) + intArray[idx];
    }
    return num;
}

/**************************************************************************
 * function: rdx_util_int2intarray
 * description: 
 * param (u32) num
 * param (u8*) intArray
 * param (u32) size
 * return (*)
 **************************************************************************/
u32 rdx_util_int2intarray(u32 num, u8* intArray, u32 size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u32 idx = 0;
    u32 tmp = 0;   
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    tmp = num;
    do {
        tmp /= 10;
        idx++;
    } while (tmp != 0);

    if (size < idx) {
        return 0;
    }

    tmp = num;
    for (idx=0; tmp!=0; idx++) {
        intArray[idx] = tmp % 10;
        tmp /= 10;
    }

    rdx_util_reverse_byte(intArray, idx);

    return idx;
}

/**************************************************************************
 * function: rdx_util_device_id_20_to_16
 * description: 
 * param (u8) *in
 * param (u8) *out
 * return (*)
 **************************************************************************/
void rdx_util_device_id_20_to_16(u8 *in, u8 *out)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 i, j;
    u8 temp[4];   
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (i=0; i<5; i++) {
        for (j=i*4; j<(i*4+4); j++) {
            if ((in[j] >= 0x30)&&(in[j] <= 0x39)) {
                temp[j-i*4] = in[j] - 0x30;
            } else if ((in[j] >= 0x41)&&(in[j] <= 0x5A)) {
                temp[j-i*4] = in[j] - 0x41 + 36;
            } else if ((in[j] >= 0x61)&&(in[j] <= 0x7A)) {
                temp[j-i*4] = in[j] - 0x61 + 10;
            } else {
            }
        }

        out[i*3] = temp[0]&0x3F;
        out[i*3] <<= 2;
        out[i*3] |= ((temp[1]>>4)&0x03);

        out[i*3+1] = temp[1]&0x0F;
        out[i*3+1] <<= 4;
        out[i*3+1] |= ((temp[2]>>2)&0x0F);

        out[i*3+2] = temp[2]&0x03;
        out[i*3+2] <<= 6;
        out[i*3+2] |= temp[3]&0x3F;
    }

    out[15] = 0xFF;
}

/**************************************************************************
 * function: rdx_util_device_id_16_to_20
 * description: 
 * param (u8) *in
 * param (u8) *out
 * return (*)
 **************************************************************************/
void rdx_util_device_id_16_to_20(u8 *in, u8 *out)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    u8 i, j;
    u8 temp[4];

    for (i=0; i<5; i++) {
        j = i*3;
        temp[j-i*3] = (in[j]>>2)&0x3F;
        temp[j-i*3+1] = in[j]&0x03;
        temp[j-i*3+1] <<= 4;
        temp[j-i*3+1] |= (in[j+1]>>4)&0x0F;
        temp[j-i*3+2] = (in[j+1]&0x0F)<<2;
        temp[j-i*3+2] |= ((in[j+2]&0xC0)>>6)&0x03;
        temp[j-i*3+3] = in[j+2]&0x3F;

        for (j=i*4; j<(i*4+4); j++) {
            if (temp[j-i*4] <= 9) {
                out[j] = temp[j-i*4]+0x30;
            } else if ((temp[j-i*4] >= 10)&&(temp[j-i*4] <= 35)) {
                out[j] = temp[j-i*4] + 87;
            } else if ((temp[j-i*4] >= 36)&&(temp[j-i*4] <= 61)) {
                out[j] = temp[j-i*4] + 29;
            } else {
            }
        }
    }
}

/**************************************************************************
 * function: rdx_util_reverse_byte
 * description: 
 * param (void*) buf
 * param (u32) size
 * return (*)
 **************************************************************************/
u32 rdx_util_reverse_byte(void* buf, u32 size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8* p_tmp = buf;
    u8  tmp;   
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (u32 idx=0; idx<size/2; idx++) {
        tmp = *(p_tmp+idx);
        *(p_tmp+idx) = *(p_tmp+size-1-idx);
        *(p_tmp+size-1-idx) = tmp;
    }
    return 0;
}

/**************************************************************************
 * function: rdx_util_count_one_in_num
 * description: 
 * param (u32) num
 * return (*)
 **************************************************************************/
u32 rdx_util_count_one_in_num(u32 num)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    num = (num&0x55555555) + ((num>>1)&0x55555555);
    num = (num&0x33333333) + ((num>>2)&0x33333333);
    num = (num&0x0f0f0f0f) + ((num>>4)&0x0f0f0f0f);
    num = (num&0x00ff00ff) + ((num>>8)&0x00ff00ff);
    num = (num&0x0000ffff) + ((num>>16)&0x0000ffff);

    return num;
}

/**************************************************************************
 * function: rdx_util_buffer_value_is_all_x
 * description: 
 * param (u8) *buf
 * param (u32) size
 * param (u8) x_value
 * return (*)
 **************************************************************************/
bool rdx_util_buffer_value_is_all_x(const u8 *buf, u32 size, u8 x_value)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (u32 idx = 0; idx<size; idx++) {
        if (buf[idx] != x_value) {
            return FALSE;
        }
    }
    return TRUE;
}

/**************************************************************************
 * function: rdx_util_is_word_aligned
 * description: 
 * param (void const*) p
 * return (*)
 **************************************************************************/
bool rdx_util_is_word_aligned(void const* p)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return (((u32)p & 0x03) == 0);
}

/**************************************************************************
 * function: rdx_util_search_symbol_index
 * description: 
 * param (u8) *buf
 * param (u32) size
 * param (u8) symbol
 * param (u8) index
 * return (*)
 **************************************************************************/
s32 rdx_util_search_symbol_index(u8 *buf, u32 size, u8 symbol, u8 index[])
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    s32 i;
    u8 index_buf[64] = {0};
    u8 symbol_cnt = 0;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (buf == NULL || size == 0 || index == NULL) {
        return symbol_cnt;
    }

    for (i=0; i<size; i++) {
        if (buf[i] == symbol) {
            index_buf[symbol_cnt] = i;
            symbol_cnt += 1;

            if (symbol_cnt >= sizeof(index_buf)) {
                /* error, too many symbols */
                break;
            }
        }
    }

    if (symbol_cnt != 0) {
        memcpy(index, index_buf, symbol_cnt);
    }

    return symbol_cnt;
}

/**************************************************************************
 * function: rdx_util_base64_encode
 * description: 
 * param (u8) *input
 * param (u16) inlen
 * param (u8) *output
 * param (u16) *outlen
 * return (*)
 **************************************************************************/
u8 rdx_util_base64_encode(const u8 *input, u16 inlen, u8 *output, u16 *outlen)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    const char *base64_tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    u32 off = 0;
    u32 i = 0;
    u8 tmp1, tmp2, tmp3;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (NULL == input || NULL == output || NULL == outlen)
        return 1;

    if (*outlen < ((inlen + 2) / 3) * 4)
        return 1;

    for (i = 0; i < inlen - 2; i += 3) {
        tmp1 = input[i];
        tmp2 = input[i + 1];
        tmp3 = input[i + 2];
        
        output[off++] = base64_tbl[tmp1 >> 2];
        output[off++] = base64_tbl[((tmp1 & 0x03) << 4) | (tmp2 >> 4)];
        output[off++] = base64_tbl[((tmp2 & 0x0F) << 2) | (tmp3 >> 6)];
        output[off++] = base64_tbl[tmp3 & 0x3F];
    }

    if (i < inlen) {
        tmp1 = input[i];
        output[off++] = base64_tbl[tmp1 >> 2];
        
        if (i + 1 < inlen) {
            tmp2 = input[i + 1];
            output[off++] = base64_tbl[((tmp1 & 0x03) << 4) | (tmp2 >> 4)];
            output[off++] = base64_tbl[((tmp2 & 0x0F) << 2)];
        } else {
            output[off++] = base64_tbl[((tmp1 & 0x03) << 4)];
            output[off++] = '=';
        }
        output[off++] = '=';
    }

    *outlen = off;
    return 0;
}

/**************************************************************************
 * function: rdx_util_base64_decode
 * description: 
 * param (u8) *input
 * param (u16) inlen
 * param (u8) *output
 * param (u16) *outlen
 * return (*)
 **************************************************************************/
u8 rdx_util_base64_decode(const u8 *input, u16 inlen, u8 *output, u16 *outlen)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    const char *base64_tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    u8 reverse_tbl[256] = {0};
    u32 off = 0;
    u32 i = 0;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (NULL == input)
        return 1;

    if (inlen == 0)
        inlen = strlen((char*)input);

    if (inlen == 0 || (inlen % 4 != 0))
        return 1;

    for (i = 0; i < 64; i++) {
        reverse_tbl[base64_tbl[i]] = i;
    }

    for (i = 0; i < inlen - 4; i+=4) {
        output[off++] = (reverse_tbl[input[i]] << 2) | ((reverse_tbl[input[i + 1]] >> 4) & 0xFF);
        output[off++] = (reverse_tbl[input[i+1]] << 4) | ((reverse_tbl[input[i + 2]] >> 2) & 0xFF);
        output[off++] = (reverse_tbl[input[i+2]] << 6) | ((reverse_tbl[input[i + 3]]) & 0xFF);
    }

    if (input[i + 2] == '=') {
        output[off++] = (reverse_tbl[input[i]] << 2) | ((reverse_tbl[input[i + 1]] >> 4) & 0xFF);
    } else if (input[i + 3] == '=') {
        output[off++] = (reverse_tbl[input[i]] << 2) | ((reverse_tbl[input[i + 1]] >> 4) & 0xFF);
        output[off++] = (reverse_tbl[input[i+1]] << 4) | ((reverse_tbl[input[i + 2]] >> 2) & 0xFF);
    } else {
        output[off++] = (reverse_tbl[input[i]] << 2) | ((reverse_tbl[input[i + 1]] >> 4) & 0xFF);
        output[off++] = (reverse_tbl[input[i+1]] << 4) | ((reverse_tbl[input[i + 2]] >> 2) & 0xFF);
        output[off++] = (reverse_tbl[input[i+2]] << 6) | ((reverse_tbl[input[i + 3]]) & 0xFF);
    }

    if (NULL != outlen)
        *outlen = off;

    return 0;
}

/**************************************************************************
 * function: delchar
 * description: 
 * param (u8) *s
 * param (u16) len
 * param (u8) match_char
 * return (*)
 **************************************************************************/
static s32 delchar(u8 *s, u16 len, u8 match_char)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    s32 i, j;
    s32 counter = 0;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (i = 0; i < len; i++) {
        if (s[i] == match_char) {
            counter++;

            for (j = i; j < len; j++) {
                s[j] = s[j+1];
                i--;
            }
        }
    }
    return counter;
}

/**************************************************************************
 * function: rdx_util_ecc_key_pem2hex
 * description: 
 * param (u8) *pem
 * param (u8) *key
 * param (u16) *key_len
 * return (*)
 **************************************************************************/
s32 rdx_util_ecc_key_pem2hex(const u8 *pem, u8 *key, u16 *key_len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    u8 buf1[256] = {0};
    u8 buf2[256] = {0};
    u16 inlen = 0;
    u16 i = 0;
    u16 len, len1, len2 = 0;

    if (NULL == pem)
        return 0;

    inlen = strlen((void*)pem);

    if (inlen > 256)
        return 0;

    //head
    if ((pem[0] != '-') || (pem[1] != '-') || (pem[2] != '-') || (pem[3] != '-') || (pem[4] != '-'))
        return 0;

    //tail
    if ((pem[inlen-1] != '-') || (pem[inlen-2] != '-') || (pem[inlen-3] != '-') || (pem[inlen-4] != '-') || (pem[inlen-5] != '-'))
        return 0;

    //find head end
    for (i=5; i<inlen-5; i++) {
        if (pem[i] == '-') {
            if ((pem[i+1] != '-') || (pem[i+2] != '-') || (pem[i+3] != '-') || (pem[i+4] != '-'))
                return 0;

            len1 = i+5;
            break;
        }
    }

    //remove head
    memcpy(buf1, pem+len1, inlen-len1);

    //find tail
    for (i=0; i<inlen-len1; i++) {
        if (buf1[i] == '-') {
            if ((buf1[i+1] != '-') || (buf1[i+2] != '-') || (buf1[i+3] != '-') || (buf1[i+4] != '-'))
                return 0;

            len2 = i;
            break;
        }
    }
    len = len2;

    //remove \n
    len1 = delchar(buf1, len, '\n');
    len = len - len1;
    //remove \r
    len1 = delchar(buf1, len, '\r');
    len = len - len1;

    //decode
    rdx_util_base64_decode((u8 *)buf1, len, (u8 *)buf2, (u16 *)&len2);

    //next is asn.1 decode
    if (buf2[0] != 0x30)//0x30
        return 0;

    len1 = buf2[1]; //0x30 0x41
    if ((len1+2) != len2)
        return 0;

    if (buf2[2] != 0x02)//0x30 0x41 0x20 0x01 0x00
        return 0;

    if (buf2[5] != 0x30)
        return 0;

    len1 = buf2[6];

    if (buf2[7+len1] != 0x04)//0x04
        return 0;

//    len2 = buf2[8+len1]; //0x04 0x27

    if (buf2[9+len1] != 0x30)//0x30
        return 0;

//    len = buf2[10+len1]; //0x25

    if (buf2[11+len1] != 0x02)//0x02
        return 0;

    if (buf2[14+len1] != 0x04)//0x04
        return 0;

    *key_len = buf2[15+len1];
    memcpy(key, &buf2[16+len1], *key_len);

    return 1;
}

/**************************************************************************
 * function: rdx_util_ecc_sign_secp256r1_extract_raw_from_der
 * description: 
 * param (u8) *der
 * param (u8) *raw_rs
 * return (*)
 **************************************************************************/
s32 rdx_util_ecc_sign_secp256r1_extract_raw_from_der(const u8 *der, u8 *raw_rs)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    /* extract r + s from der */
    int pos = 0;
    u8 raw[64] = {0};

    if (der == NULL || raw_rs == NULL)
        return 0;

    if (der[3] != 0x20) {
        memcpy(raw, &der[5], 32);
        pos = 5+32;
    } else {
        memcpy(raw, &der[4], 32);
        pos = 4+32;
    }

    // 37
    if (der[pos+1] != 0x20)
        memcpy(&raw[32], &der[pos+3], 32);
    else
        memcpy(&raw[32], &der[pos+2], 32);

    memcpy(raw_rs, raw, SIZEOF(raw));
    return 1;
}

/**************************************************************************
 * function: rdx_util_shell_sort
 * description: 
 * param (int*) buf
 * param (int) size
 * return (*)
 **************************************************************************/
u32 rdx_util_shell_sort(int* buf, int size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int i;
    int j;
    int temp;
    int gap;  //Step size   
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (gap = size / 2; gap >= 1; gap /= 2) {
        for (i = 0 + gap; i < size; i += gap) {
            temp = buf[i];
            j = i - gap;
            while (j >= 0 && buf[j] > temp) {
                buf[j + gap] = buf[j];
                j -= gap;
            }
            buf[j + gap] = temp;
        }
    }
    return 0;
}

/**************************************************************************
 * function: rdx_util_str_hexchar2int
 * description: 
 * param (u8) hexChar
 * return (*)
 **************************************************************************/
u8 rdx_util_str_hexchar2int(u8 hexChar)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    switch (hexChar) {
        case '0':return 0;
        case '1':return 1;
        case '2':return 2;
        case '3':return 3;
        case '4':return 4;
        case '5':return 5;
        case '6':return 6;
        case '7':return 7;
        case '8':return 8;
        case '9':return 9;
        case 'a':
        case 'A':return 10;
        case 'b':
        case 'B':return 11;
        case 'c':
        case 'C':return 12;
        case 'd':
        case 'D':return 13;
        case 'e':
        case 'E':return 14;
        case 'f':
        case 'F':return 15;
        default: return (u8)-1;
    }
}

/**************************************************************************
 * function: rdx_util_str_int2hexchar
 * description: 
 * param (bool) isHEX
 * param (u8) intNum
 * return (*)
 **************************************************************************/
u8 rdx_util_str_int2hexchar(bool isHEX, u8 intNum)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    switch (intNum) {
        case 0:return '0';
        case 1:return '1';
        case 2:return '2';
        case 3:return '3';
        case 4:return '4';
        case 5:return '5';
        case 6:return '6';
        case 7:return '7';
        case 8:return '8';
        case 9:return '9';
        case 10:return (isHEX ? 'A' : 'a');
        case 11:return (isHEX ? 'B' : 'b');
        case 12:return (isHEX ? 'C' : 'c');
        case 13:return (isHEX ? 'D' : 'd');
        case 14:return (isHEX ? 'E' : 'e');
        case 15:return (isHEX ? 'F' : 'f');
        default:return (u8)-1;
    }
}

/**************************************************************************
 * function: rdx_util_str_hexstr2int
 * description: 
 * param (u8*) hexStr
 * param (u32) size
 * param (u32*) num
 * return (*)
 **************************************************************************/
u32 rdx_util_str_hexstr2int(u8* hexStr, u32 size, u32* num)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    *num = 0;
    for (u32 idx=0; idx<size; idx++) {
        u8 tmp = rdx_util_str_hexchar2int(hexStr[idx]);
        if (tmp == (u8)-1) {
            return 1;
        }

        (*num) = (*num)<<4;
        (*num) += tmp;
    }
    return 0;
}

/**************************************************************************
 * function: rdx_util_str_int2hexstr
 * description: 
 * param (bool) isHEX
 * param (u32) num
 * param (u8*) hexStr
 * param (u32) size
 * return (*)
 **************************************************************************/
u32 rdx_util_str_int2hexstr(bool isHEX, u32 num, u8* hexStr, u32 size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u32 idx = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    hexStr[idx++] = rdx_util_str_int2hexchar(isHEX, num%16);
    num = num/16;

    while (num >= 16) {
        hexStr[idx++] = rdx_util_str_int2hexchar(isHEX, num%16);
        num = num/16;
    }

    hexStr[idx++] = rdx_util_str_int2hexchar(isHEX, num);

    if (idx < size) {
        memset(&hexStr[idx], '0', size-idx);
    }

    rdx_util_reverse_byte(hexStr, size);

    return idx;
}

/**************************************************************************
 * function: rdx_util_str_intstr2int
 * description: 
 * param (u8*) intStr
 * param (u32) size
 * param (u32*) num
 * return (*)
 **************************************************************************/
u32 rdx_util_str_intstr2int(u8* intStr, u32 size, u32* num)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    *num = 0;
    for (u32 idx=0; idx<size; idx++) {
        u8 tmp = rdx_util_str_hexchar2int(intStr[idx]);
        if (tmp == (u8)-1 || tmp > 9) {
            return 1;
        }

        (*num) = (*num)*10;
        (*num) += tmp;
    }
    return 0;
}

/**************************************************************************
 * function: rdx_util_str_intstr2int_with_negative
 * description: 
 * param (char*) intStr
 * param (u32) size
 * param (s32*) num
 * return (*)
 **************************************************************************/
u32 rdx_util_str_intstr2int_with_negative(char* intStr, u32 size, s32* num)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    s32 cal_num = 0;
    bool is_negative = FALSE;   
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (intStr == NULL || size == 0 || num == NULL) {
        return 1;
    }

    for (u32 idx=0; idx<size; idx++) {
        u8 tmp = rdx_util_str_hexchar2int(intStr[idx]);

        if ((idx == 0) && (intStr[idx] == 0x2D)) {
            is_negative = TRUE;
            continue;
        } else if (tmp == (u8)-1 || tmp > 9) {
            return 1;
        }

        cal_num *= 10;
        cal_num += tmp;
    }

    if (is_negative) {
        cal_num = (-1*cal_num);
    }

    *num = cal_num;
    return 0;
}

/**************************************************************************
 * function: rdx_util_str_int2intstr
 * description: 
 * param (u32) num
 * param (u8*) intStr
 * param (u32) size
 * return (*)
 **************************************************************************/
u32 rdx_util_str_int2intstr(u32 num, u8* intStr, u32 size)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u32 idx = 0;
    u32 tmp = 0;   
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    tmp = num;
    do {
        tmp /= 10;
        idx++;
    } while (tmp != 0);

    if (size < idx) {
        return 0;
    }

    tmp = num;
    for (idx=0; tmp!=0; idx++) {
        intStr[idx] = rdx_util_str_int2hexchar(true, tmp % 10);
        tmp /= 10;
    }

    rdx_util_reverse_byte(intStr, idx);

    return idx;
}

/**************************************************************************
 * function: rdx_util_str_hexstr2hexarray
 * description: 
 * param (u8*) hexStr
 * param (u32) size
 * param (u8*) hexArray
 * return (*)
 **************************************************************************/
u32 rdx_util_str_hexstr2hexarray(u8* hexStr, u32 size, u8* hexArray)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 hex_num = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (u32 idx=0; idx<size; idx++) {
        u8 tmp = rdx_util_str_hexchar2int(hexStr[idx]);
        if (tmp == (u8)-1) {
            return 1;
        }

        hex_num <<= 4;
        hex_num |= tmp;

        if ((idx & 1) == 1) {
            hexArray[idx>>1] = hex_num;
            hex_num = 0;
        }
    }
    return 0;
}

/**************************************************************************
 * function: rdx_util_str_hexarray2hexstr
 * description: 
 * param (bool) isHEX
 * param (u8*) hexArray
 * param (u32) size
 * param (u8*) hexStr
 * return (*)
 **************************************************************************/
u32 rdx_util_str_hexarray2hexstr(bool isHEX, u8* hexArray, u32 size, u8* hexStr)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u32 idx;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (idx=0; idx<size; idx++) {
        u8 high = hexArray[idx]>>4;
        u8 low  = hexArray[idx]&0x0F;

        hexStr[idx*2] = rdx_util_str_int2hexchar(isHEX, high);
        hexStr[idx*2+1] = rdx_util_str_int2hexchar(isHEX, low);
    }
//    hexstr[idx*2] = '0'; //To prevent the length of the hexStr array from being insufficient, it can be added if needed
    return 0;
}

/**************************************************************************
 * function: find_char
 * description: 
 * param (u8) *buf
 * param (u16) len
 * param (u8) data_byte
 * return (*)
 **************************************************************************/
static inline s32 find_char(u8 *buf, u16 len, u8 data_byte)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    for (u16 i=0; i<len; i++) {
        if (buf[i] == data_byte) {
            return i;
        }
    }
    return -1;
}

/**************************************************************************
 * function: rdx_util_get_value_by_key
 * description: 
 * param (u8) *input_buf
 * param (u16) input_len
 * param (u8) *key
 * param (u16) key_len
 * param (u8) *value_data
 * param (u16) *value_len
 * return (*)
 **************************************************************************/
u8 rdx_util_get_value_by_key(u8 *input_buf, u16 input_len, u8 *key, u16 key_len, u8 *value_data, u16 *value_len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u16 i = 0;
    u8 temp[2];
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if ((input_len == 0) || (key_len == 0) || (input_len < key_len) || (NULL == value_data)) {
        return 2;
    }
    temp[0] = input_buf[0];
    temp[1] = input_buf[input_len-1];
    input_buf[0] = ','; //easy for search: ,xx:xx,xx:xx,
    input_buf[input_len-1] = ',';

    while (i < input_len) {
        u16 base = i;
        s16 index1 = find_char(&input_buf[base], input_len, ',');
        if (index1 == -1)return 0;
        s16 index2 = find_char(&input_buf[base+index1], input_len-index1, ':');
        if (index2 == -1)return 0;
        s16 index3 = find_char(&input_buf[base+index1+index2], input_len-index1-index2, ',');
        if (index3 == -1)return 0;

        s16 find_key_pos = base + index1 + 1;
        s16 find_key_len = index2 - 1;
        s16 find_value_pos = base + index1 + index2 + 1;
        s16 find_value_len = index3 - 1;

        if (find_key_len >= 2 &&//delete "\"...\""
                input_buf[find_key_pos] == '"' &&
                input_buf[find_key_pos+find_key_len-1] == '"') {
            find_key_pos++;
            find_key_len -= 2;
        }
        if (find_value_len >= 2 &&//delete
                input_buf[find_value_pos] == '"' &&
                input_buf[find_value_pos+find_value_len-1] == '"') {
            find_value_pos++;
            find_value_len -= 2;
        }

        if (find_key_len != key_len) {
            i = base + index3 + index2 + index1;
            continue;
        }

        u8 find = 1;
        for (u16 j=0; j<find_key_len; j++) {
            if (input_buf[find_key_pos+j] != key[j]) {
                find = 0;
                i = base + index3 + index2 + index1;
                break;
            }
        }
        if (find) {
            for (u16 k=0; k<find_value_len; k++) {
                value_data[k] = input_buf[find_value_pos+k];
            }
            input_buf[0] = temp[0];
            input_buf[input_len-1] = temp[1];
            *value_len = find_value_len;
            return 1;
        }
    }
    input_buf[0] = temp[0];
    input_buf[input_len-1] = temp[1];
    return 0;
}

/**************************************************************************
 * function: rdx_util_get_value_by_key_to_int
 * description: 
 * param (u8) *input_buf
 * param (u16) input_len
 * param (u8) *key
 * param (u8) key_len
 * param (u32) *result
 * return (*)
 **************************************************************************/
u8 rdx_util_get_value_by_key_to_int(u8 *input_buf, u16 input_len, u8 *key, u8 key_len, u32 *result)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 value_data[20];
    u16 value_len = 0;
    u8 ret, ok = 0;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    ret = rdx_util_get_value_by_key(input_buf, input_len, key, key_len, value_data, &value_len);
    if (ret == 1) {
        if (0 == rdx_util_str_intstr2int(value_data, value_len, result)) {
            ok = 1;
        }
    }
    return ok;
}

/**************************************************************************
 * function: rdx_util_get_value_by_key_to_hex
 * description: 
 * param (u8) *input_buf
 * param (u16) input_len
 * param (u8) *key
 * param (u8) key_len
 * param (u32) *result
 * return (*)
 **************************************************************************/
u8 rdx_util_get_value_by_key_to_hex(u8 *input_buf, u16 input_len, u8 *key, u8 key_len, u32 *result)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 value_data[20];
    u16 value_len = 0;
    u8 ret, ok = 0;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    ret = rdx_util_get_value_by_key(input_buf, input_len, key, key_len, value_data, &value_len);
    if (ret == 1) {
        if (0 == rdx_util_str_hexstr2int(value_data, value_len, result)) {
            ok = 1;
        }
    }
    return ok;
}

/**************************************************************************
 * function: rdx_util_get_value_by_key_to_bool
 * description: 
 * param (u8) *input_buf
 * param (u16) input_len
 * param (u8) *key
 * param (u8) key_len
 * param (u32) *result
 * return (*)
 **************************************************************************/
u8 rdx_util_get_value_by_key_to_bool(u8 *input_buf, u16 input_len, u8 *key, u8 key_len, u32 *result)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 value_data[20];
    u16 value_len = 0;
    u8 ret, ok = 0;    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    ret = rdx_util_get_value_by_key(input_buf, input_len, key, key_len, value_data, &value_len);
    if (ret == 1) {
        if (0 == memcmp(value_data, "false", 5)) {
            *result = 0;
            ok = 1;
        } else if (0 == memcmp(value_data, "true", 4)) {
            *result = 1;
            ok = 1;
        }
    }

    return ok;
}

/**************************************************************************
 * function: rdx_util_hex_to_char
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
int rdx_util_hex_to_char(const char* hex, char *out)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    int len = strlen(hex);
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(len%2 != 0){
        return -1;
    }
    for(int i = 0; i < len; i += 2){
        char *endptr;
        long val = strtol(hex + i, &endptr, 16);
        if(*endptr != '\0'){
            return -1;
        }
        *out++ = (char)val;
    }
    *out = '\0';
    return 0;
}

int rdx_util_version_str2int(char *buf, u32 len)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    u8 symbol_point_index[5] = {0};
    u8 symbol_point_cnt = 0;
    u32 version = 0;
    char ver[3];
    u16 first_pos, second_pos, third_pos;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    memset(symbol_point_index, 0, 5);
    symbol_point_cnt = rdx_util_search_symbol_index(buf, len, '.', symbol_point_index);
    if(symbol_point_cnt == 0){
        return -1;
    }
    memset(ver, 0, 3);
    first_pos = symbol_point_index[0] - 1;
    ver[2] = buf[first_pos] - '0';
    second_pos = symbol_point_index[1] - 1;
    ver[1] = buf[second_pos] - '0';
    third_pos = symbol_point_index[1] + 1;
    ver[0] = buf[third_pos] - '0';

    version = ver[2] << 16 | ver[1] << 8 | ver[0];
    y_printf("%s --> ver[2] = %d, ver[1] = %d, ver[0] = %d , version = %08X \r", __func__, ver[2], ver[1], ver[0], version);

    return version;
}