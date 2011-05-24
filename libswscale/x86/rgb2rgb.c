/*
 * software RGB to RGB converter
 * pluralize by software PAL8 to RGB converter
 *              software YUV to YUV converter
 *              software YUV to RGB converter
 * Written by Nick Kurshev.
 * palette & YUV & runtime CPU stuff by Michael (michaelni@gmx.at)
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "config.h"
#include "libavutil/x86_cpu.h"
#include "libavutil/cpu.h"
#include "libavutil/bswap.h"
#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

DECLARE_ASM_CONST(8, uint64_t, mmx_ff)       = 0x00000000000000FFULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_null)     = 0x0000000000000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_one)      = 0xFFFFFFFFFFFFFFFFULL;
DECLARE_ASM_CONST(8, uint64_t, mask32b)      = 0x000000FF000000FFULL;
DECLARE_ASM_CONST(8, uint64_t, mask32g)      = 0x0000FF000000FF00ULL;
DECLARE_ASM_CONST(8, uint64_t, mask32r)      = 0x00FF000000FF0000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask32a)      = 0xFF000000FF000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask32)       = 0x00FFFFFF00FFFFFFULL;
DECLARE_ASM_CONST(8, uint64_t, mask3216br)   = 0x00F800F800F800F8ULL;
DECLARE_ASM_CONST(8, uint64_t, mask3216g)    = 0x0000FC000000FC00ULL;
DECLARE_ASM_CONST(8, uint64_t, mask3215g)    = 0x0000F8000000F800ULL;
DECLARE_ASM_CONST(8, uint64_t, mul3216)      = 0x2000000420000004ULL;
DECLARE_ASM_CONST(8, uint64_t, mul3215)      = 0x2000000820000008ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24b)      = 0x00FF0000FF0000FFULL;
DECLARE_ASM_CONST(8, uint64_t, mask24g)      = 0xFF0000FF0000FF00ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24r)      = 0x0000FF0000FF0000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24l)      = 0x0000000000FFFFFFULL;
DECLARE_ASM_CONST(8, uint64_t, mask24h)      = 0x0000FFFFFF000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24hh)     = 0xffff000000000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24hhh)    = 0xffffffff00000000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask24hhhh)   = 0xffffffffffff0000ULL;
DECLARE_ASM_CONST(8, uint64_t, mask15b)      = 0x001F001F001F001FULL; /* 00000000 00011111  xxB */
DECLARE_ASM_CONST(8, uint64_t, mask15rg)     = 0x7FE07FE07FE07FE0ULL; /* 01111111 11100000  RGx */
DECLARE_ASM_CONST(8, uint64_t, mask15s)      = 0xFFE0FFE0FFE0FFE0ULL;
DECLARE_ASM_CONST(8, uint64_t, mask15g)      = 0x03E003E003E003E0ULL;
DECLARE_ASM_CONST(8, uint64_t, mask15r)      = 0x7C007C007C007C00ULL;
#define mask16b mask15b
DECLARE_ASM_CONST(8, uint64_t, mask16g)      = 0x07E007E007E007E0ULL;
DECLARE_ASM_CONST(8, uint64_t, mask16r)      = 0xF800F800F800F800ULL;
DECLARE_ASM_CONST(8, uint64_t, red_16mask)   = 0x0000f8000000f800ULL;
DECLARE_ASM_CONST(8, uint64_t, green_16mask) = 0x000007e0000007e0ULL;
DECLARE_ASM_CONST(8, uint64_t, blue_16mask)  = 0x0000001f0000001fULL;
DECLARE_ASM_CONST(8, uint64_t, red_15mask)   = 0x00007c0000007c00ULL;
DECLARE_ASM_CONST(8, uint64_t, green_15mask) = 0x000003e0000003e0ULL;
DECLARE_ASM_CONST(8, uint64_t, blue_15mask)  = 0x0000001f0000001fULL;

#define RGB2YUV_SHIFT 8
#define BY ((int)( 0.098*(1<<RGB2YUV_SHIFT)+0.5))
#define BV ((int)(-0.071*(1<<RGB2YUV_SHIFT)+0.5))
#define BU ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define GY ((int)( 0.504*(1<<RGB2YUV_SHIFT)+0.5))
#define GV ((int)(-0.368*(1<<RGB2YUV_SHIFT)+0.5))
#define GU ((int)(-0.291*(1<<RGB2YUV_SHIFT)+0.5))
#define RY ((int)( 0.257*(1<<RGB2YUV_SHIFT)+0.5))
#define RV ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define RU ((int)(-0.148*(1<<RGB2YUV_SHIFT)+0.5))

//Note: We have C, MMX, MMX2, 3DNOW versions, there is no 3DNOW + MMX2 one.

#define COMPILE_TEMPLATE_MMX2 0
#define COMPILE_TEMPLATE_AMD3DNOW 0
#define COMPILE_TEMPLATE_SSE2 0

//MMX versions
#undef RENAME
#define RENAME(a) a ## _MMX
#include "rgb2rgb_template.c"

//MMX2 versions
#undef RENAME
#undef COMPILE_TEMPLATE_MMX2
#define COMPILE_TEMPLATE_MMX2 1
#define RENAME(a) a ## _MMX2
#include "rgb2rgb_template.c"

//SSE2 versions
#undef RENAME
#undef COMPILE_TEMPLATE_SSE2
#define COMPILE_TEMPLATE_SSE2 1
#define RENAME(a) a ## _SSE2
#include "rgb2rgb_template.c"

//3DNOW versions
#undef RENAME
#undef COMPILE_TEMPLATE_MMX2
#undef COMPILE_TEMPLATE_SSE2
#undef COMPILE_TEMPLATE_AMD3DNOW
#define COMPILE_TEMPLATE_MMX2 0
#define COMPILE_TEMPLATE_SSE2 1
#define COMPILE_TEMPLATE_AMD3DNOW 1
#define RENAME(a) a ## _3DNOW
#include "rgb2rgb_template.c"

/*
 RGB15->RGB16 original by Strepto/Astral
 ported to gcc & bugfixed : A'rpi
 MMX2, 3DNOW optimization by Nick Kurshev
 32-bit C version, and and&add trick by Michael Niedermayer
*/

void rgb2rgb_init_x86(void)
{
    int cpu_flags = av_get_cpu_flags();

    if (HAVE_MMX      && cpu_flags & AV_CPU_FLAG_MMX)
        rgb2rgb_init_MMX();
    if (HAVE_AMD3DNOW && cpu_flags & AV_CPU_FLAG_3DNOW)
        rgb2rgb_init_3DNOW();
    if (HAVE_MMX2     && cpu_flags & AV_CPU_FLAG_MMX2)
        rgb2rgb_init_MMX2();
    if (HAVE_SSE      && cpu_flags & AV_CPU_FLAG_SSE2)
        rgb2rgb_init_SSE2();
}