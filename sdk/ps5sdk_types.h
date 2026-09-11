#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;
typedef int64_t  s64;
typedef int32_t  s32;
typedef int16_t  s16;
typedef int8_t   s8;

/* BGRA colour, as SDL wants it */
#define PS5SDK_RGB(r,g,b)   ((u32)((b)<<16|(g)<<8|(r)|0xFF000000u))
#define PS5SDK_RGBA(r,g,b,a)((u32)((b)<<16|(g)<<8|(r)|(a)<<24u))
#define PS5SDK_R(c) ((c)&0xFF)
#define PS5SDK_G(c) (((c)>>8)&0xFF)
#define PS5SDK_B(c) (((c)>>16)&0xFF)

/* the colours to hand */
#define PS5SDK_BLACK   0xFF000000u
#define PS5SDK_WHITE   0xFFFFFFFFu
#define PS5SDK_RED     PS5SDK_RGB(255,  0,  0)
#define PS5SDK_GREEN   PS5SDK_RGB(  0,255,  0)
#define PS5SDK_BLUE    PS5SDK_RGB(  0,  0,255)
#define PS5SDK_CYAN    PS5SDK_RGB(  0,217,232)
#define PS5SDK_MAGENTA PS5SDK_RGB(255, 42,109)
#define PS5SDK_YELLOW  PS5SDK_RGB(255,255,  0)
#define PS5SDK_GRAY    PS5SDK_RGB(128,128,128)
#define PS5SDK_DGRAY   PS5SDK_RGB( 30, 35, 50)
