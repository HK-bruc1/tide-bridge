#ifndef _PRINTF_H_
#define _PRINTF_H_
#define line_inf printf("%s %s %d \r\n" ,__FILE__, __func__ , __LINE__) ;
#include <stdarg.h>
#include "generic/typedef.h"
#include "dlog.h"
//#define NOFLOAT

extern int putchar(int a);
extern int puts(const char *out);
// puts 目前无法提取字符串常量
#define puts(format)  {puts(format);if(config_dlog_enable){dlog_printf((-2 & ~BIT(31)), "%s\n", (const char *)format);}}
extern void put_float(double fv);
void put_u4hex(unsigned char dat);
void put_u8hex(unsigned char dat);
void put_u16hex(unsigned short dat);
void put_u32hex(unsigned int dat);
void put_buf(const u8 *buf, int len);
int printf(const char *format, ...);
#define printf(format, ...)  {printf(format, ##__VA_ARGS__);if(config_dlog_enable){dlog_printf((-1 & ~BIT(31)), format, ##__VA_ARGS__);}}
int assert_printf(const char *format, ...);
int sprintf(char *out, const char *format, ...);
int vprintf(const char *fmt, __builtin_va_list va);
int vsnprintf(char *, unsigned long, const char *, __builtin_va_list);
int snprintf(char *buf, unsigned long size, const char *fmt, ...);
int print(char **out, char *end, const char *format, va_list args);
//int snprintf(char *, unsigned long, const char *, ...);

int sscanf(const char *buf, const char *fmt, ...);   //BUG: 多个参数? 最后又空格?

//int perror(const char *fmt, ...);

#endif
