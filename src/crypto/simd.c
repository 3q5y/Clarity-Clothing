
/* $Id: simd.c 227 2010-06-16 17:28:38Z tp $ */
/*
 * SIMD implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2007-2010  Projet RNRT SAPHIR
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ===========================(LICENSE END)=============================
 *
 * @author   Thomas Pornin <thomas.pornin@cryptolog.com>
 */

#include <stddef.h>
#include <string.h>
#include <limits.h>

#include "sph_simd.h"

#ifdef __cplusplus
extern "C"{
#endif

#if SPH_SMALL_FOOTPRINT && !defined SPH_SMALL_FOOTPRINT_SIMD
#define SPH_SMALL_FOOTPRINT_SIMD   1
#endif

#ifdef _MSC_VER
#pragma warning (disable: 4146)
#endif

typedef sph_u32 u32;
typedef sph_s32 s32;
#define C32     SPH_C32
#define T32     SPH_T32
#define ROL32   SPH_ROTL32

#define XCAT(x, y)    XCAT_(x, y)
#define XCAT_(x, y)   x ## y

/*
 * The powers of 41 modulo 257. We use exponents from 0 to 255, inclusive.
 */
static const s32 alpha_tab[] = {
	  1,  41, 139,  45,  46,  87, 226,  14,  60, 147, 116, 130,
	190,  80, 196,  69,   2,  82,  21,  90,  92, 174, 195,  28,
	120,  37, 232,   3, 123, 160, 135, 138,   4, 164,  42, 180,
	184,  91, 133,  56, 240,  74, 207,   6, 246,  63,  13,  19,
	  8,  71,  84, 103, 111, 182,   9, 112, 223, 148, 157,  12,
	235, 126,  26,  38,  16, 142, 168, 206, 222, 107,  18, 224,
	189,  39,  57,  24, 213, 252,  52,  76,  32,  27,  79, 155,
	187, 214,  36, 191, 121,  78, 114,  48, 169, 247, 104, 152,
	 64,  54, 158,  53, 117, 171,  72, 125, 242, 156, 228,  96,
	 81, 237, 208,  47, 128, 108,  59, 106, 234,  85, 144, 250,
	227,  55, 199, 192, 162, 217, 159,  94, 256, 216, 118, 212,
	211, 170,  31, 243, 197, 110, 141, 127,  67, 177,  61, 188,
	255, 175, 236, 167, 165,  83,  62, 229, 137, 220,  25, 254,
	134,  97, 122, 119, 253,  93, 215,  77,  73, 166, 124, 201,
	 17, 183,  50, 251,  11, 194, 244, 238, 249, 186, 173, 154,
	146,  75, 248, 145,  34, 109, 100, 245,  22, 131, 231, 219,
	241, 115,  89,  51,  35, 150, 239,  33,  68, 218, 200, 233,
	 44,   5, 205, 181, 225, 230, 178, 102,  70,  43, 221,  66,
	136, 179, 143, 209,  88,  10, 153, 105, 193, 203,  99, 204,
	140,  86, 185, 132,  15, 101,  29, 161, 176,  20,  49, 210,
	129, 149, 198, 151,  23, 172, 113,   7,  30, 202,  58,  65,
	 95,  40,  98, 163
};

/*
 * Ranges:
 *   REDS1: from -32768..98302 to -383..383
 *   REDS2: from -2^31..2^31-1 to -32768..98302
 */
#define REDS1(x)    (((x) & 0xFF) - ((x) >> 8))
#define REDS2(x)    (((x) & 0xFFFF) + ((x) >> 16))

/*
 * If, upon entry, the values of q[] are all in the -N..N range (where
 * N >= 98302) then the new values of q[] are in the -2N..2N range.
 *
 * Since alpha_tab[v] <= 256, maximum allowed range is for N = 8388608.
 */
#define FFT_LOOP(rb, hk, as, id)   do { \
		size_t u, v; \
		s32 m = q[(rb)]; \
		s32 n = q[(rb) + (hk)]; \
		q[(rb)] = m + n; \
		q[(rb) + (hk)] = m - n; \
		u = v = 0; \
		goto id; \
		for (; u < (hk); u += 4, v += 4 * (as)) { \
			s32 t; \
			m = q[(rb) + u + 0]; \
			n = q[(rb) + u + 0 + (hk)]; \
			t = REDS2(n * alpha_tab[v + 0 * (as)]); \
			q[(rb) + u + 0] = m + t; \
			q[(rb) + u + 0 + (hk)] = m - t; \
		id: \
			m = q[(rb) + u + 1]; \
			n = q[(rb) + u + 1 + (hk)]; \
			t = REDS2(n * alpha_tab[v + 1 * (as)]); \
			q[(rb) + u + 1] = m + t; \
			q[(rb) + u + 1 + (hk)] = m - t; \
			m = q[(rb) + u + 2]; \
			n = q[(rb) + u + 2 + (hk)]; \
			t = REDS2(n * alpha_tab[v + 2 * (as)]); \
			q[(rb) + u + 2] = m + t; \
			q[(rb) + u + 2 + (hk)] = m - t; \
			m = q[(rb) + u + 3]; \
			n = q[(rb) + u + 3 + (hk)]; \
			t = REDS2(n * alpha_tab[v + 3 * (as)]); \
			q[(rb) + u + 3] = m + t; \
			q[(rb) + u + 3 + (hk)] = m - t; \
		} \
	} while (0)

/*
 * Output ranges:
 *   d0:   min=    0   max= 1020
 *   d1:   min=  -67   max= 4587
 *   d2:   min=-4335   max= 4335
 *   d3:   min=-4147   max=  507
 *   d4:   min= -510   max=  510
 *   d5:   min= -252   max= 4402
 *   d6:   min=-4335   max= 4335
 *   d7:   min=-4332   max=  322
 */
#define FFT8(xb, xs, d)   do { \
		s32 x0 = x[(xb)]; \
		s32 x1 = x[(xb) + (xs)]; \
		s32 x2 = x[(xb) + 2 * (xs)]; \
		s32 x3 = x[(xb) + 3 * (xs)]; \
		s32 a0 = x0 + x2; \
		s32 a1 = x0 + (x2 << 4); \
		s32 a2 = x0 - x2; \
		s32 a3 = x0 - (x2 << 4); \
		s32 b0 = x1 + x3; \
		s32 b1 = REDS1((x1 << 2) + (x3 << 6)); \
		s32 b2 = (x1 << 4) - (x3 << 4); \
		s32 b3 = REDS1((x1 << 6) + (x3 << 2)); \
		d ## 0 = a0 + b0; \
		d ## 1 = a1 + b1; \
		d ## 2 = a2 + b2; \
		d ## 3 = a3 + b3; \
		d ## 4 = a0 - b0; \
		d ## 5 = a1 - b1; \
		d ## 6 = a2 - b2; \
		d ## 7 = a3 - b3; \
	} while (0)

/*
 * When k=16, we have alpha=2. Multiplication by alpha^i is then reduced
 * to some shifting.
 *
 * Output: within -591471..591723
 */
#define FFT16(xb, xs, rb)   do { \
		s32 d1_0, d1_1, d1_2, d1_3, d1_4, d1_5, d1_6, d1_7; \
		s32 d2_0, d2_1, d2_2, d2_3, d2_4, d2_5, d2_6, d2_7; \
		FFT8(xb, (xs) << 1, d1_); \
		FFT8((xb) + (xs), (xs) << 1, d2_); \
		q[(rb) +  0] = d1_0 + d2_0; \
		q[(rb) +  1] = d1_1 + (d2_1 << 1); \
		q[(rb) +  2] = d1_2 + (d2_2 << 2); \
		q[(rb) +  3] = d1_3 + (d2_3 << 3); \
		q[(rb) +  4] = d1_4 + (d2_4 << 4); \
		q[(rb) +  5] = d1_5 + (d2_5 << 5); \
		q[(rb) +  6] = d1_6 + (d2_6 << 6); \
		q[(rb) +  7] = d1_7 + (d2_7 << 7); \
		q[(rb) +  8] = d1_0 - d2_0; \
		q[(rb) +  9] = d1_1 - (d2_1 << 1); \
		q[(rb) + 10] = d1_2 - (d2_2 << 2); \
		q[(rb) + 11] = d1_3 - (d2_3 << 3); \
		q[(rb) + 12] = d1_4 - (d2_4 << 4); \
		q[(rb) + 13] = d1_5 - (d2_5 << 5); \
		q[(rb) + 14] = d1_6 - (d2_6 << 6); \
		q[(rb) + 15] = d1_7 - (d2_7 << 7); \
	} while (0)

/*
 * Output range: |q| <= 1183446
 */
#define FFT32(xb, xs, rb, id)   do { \
		FFT16(xb, (xs) << 1, rb); \
		FFT16((xb) + (xs), (xs) << 1, (rb) + 16); \
		FFT_LOOP(rb, 16, 8, id); \
	} while (0)

/*
 * Output range: |q| <= 2366892
 */
#define FFT64(xb, xs, rb, id)   do { \
		FFT32(xb, (xs) << 1, rb, XCAT(id, a)); \
		FFT32((xb) + (xs), (xs) << 1, (rb) + 32, XCAT(id, b)); \
		FFT_LOOP(rb, 32, 4, id); \
	} while (0)

#if SPH_SMALL_FOOTPRINT_SIMD

static void
fft32(unsigned char *x, size_t xs, s32 *q)
{
	size_t xd;

	xd = xs << 1;
	FFT16(0, xd, 0);
	FFT16(xs, xd, 16);
	FFT_LOOP(0, 16, 8, label_);
}

#define FFT128(xb, xs, rb, id)   do { \
		fft32(x + (xb) + ((xs) * 0), (xs) << 2, &q[(rb) +  0]); \
		fft32(x + (xb) + ((xs) * 2), (xs) << 2, &q[(rb) + 32]); \
		FFT_LOOP(rb, 32, 4, XCAT(id, aa)); \
		fft32(x + (xb) + ((xs) * 1), (xs) << 2, &q[(rb) + 64]); \
		fft32(x + (xb) + ((xs) * 3), (xs) << 2, &q[(rb) + 96]); \
		FFT_LOOP((rb) + 64, 32, 4, XCAT(id, ab)); \
		FFT_LOOP(rb, 64, 2, XCAT(id, a)); \
	} while (0)

#else

/*
 * Output range: |q| <= 4733784
 */
#define FFT128(xb, xs, rb, id)   do { \
		FFT64(xb, (xs) << 1, rb, XCAT(id, a)); \
		FFT64((xb) + (xs), (xs) << 1, (rb) + 64, XCAT(id, b)); \
		FFT_LOOP(rb, 64, 2, id); \
	} while (0)

#endif

/*
 * For SIMD-384 / SIMD-512, the fully unrolled FFT yields a compression
 * function which does not fit in the 32 kB L1 cache of a typical x86
 * Intel. We therefore add a function call layer at the FFT64 level.
 */

static void
fft64(unsigned char *x, size_t xs, s32 *q)
{
	size_t xd;

	xd = xs << 1;
	FFT32(0, xd, 0, label_a);
	FFT32(xs, xd, 32, label_b);
	FFT_LOOP(0, 32, 4, label_);
}

/*
 * Output range: |q| <= 9467568
 */
#define FFT256(xb, xs, rb, id)   do { \
		fft64(x + (xb) + ((xs) * 0), (xs) << 2, &q[(rb) +   0]); \
		fft64(x + (xb) + ((xs) * 2), (xs) << 2, &q[(rb) +  64]); \
		FFT_LOOP(rb, 64, 2, XCAT(id, aa)); \
		fft64(x + (xb) + ((xs) * 1), (xs) << 2, &q[(rb) + 128]); \
		fft64(x + (xb) + ((xs) * 3), (xs) << 2, &q[(rb) + 192]); \
		FFT_LOOP((rb) + 128, 64, 2, XCAT(id, ab)); \
		FFT_LOOP(rb, 128, 1, XCAT(id, a)); \
	} while (0)

/*
 * alpha^(127*i) mod 257
 */
static const unsigned short yoff_s_n[] = {
	  1,  98,  95,  58,  30, 113,  23, 198, 129,  49, 176,  29,
	 15, 185, 140,  99, 193, 153,  88, 143, 136, 221,  70, 178,
	225, 205,  44, 200,  68, 239,  35,  89, 241, 231,  22, 100,
	 34, 248, 146, 173, 249, 244,  11,  50,  17, 124,  73, 215,
	253, 122, 134,  25, 137,  62, 165, 236, 255,  61,  67, 141,
	197,  31, 211, 118, 256, 159, 162, 199, 227, 144, 234,  59,
	128, 208,  81, 228, 242,  72, 117, 158,  64, 104, 169, 114,
	121,  36, 187,  79,  32,  52, 213,  57, 189,  18, 222, 168,
	 16,  26, 235, 157, 223,   9, 111,  84,   8,  13, 246, 207,
	240, 133, 184,  42,   4, 135, 123, 232, 120, 195,  92,  21,
	  2, 196, 190, 116,  60, 226,  46, 139
};

/*
 * alpha^(127*i) + alpha^(125*i) mod 257
 */
static const unsigned short yoff_s_f[] = {
	  2, 156, 118, 107,  45, 212, 111, 162,  97, 249, 211,   3,
	 49, 101, 151, 223, 189, 178, 253, 204,  76,  82, 232,  65,
	 96, 176, 161,  47, 189,  61, 248, 107,   0, 131, 133, 113,
	 17,  33,  12, 111, 251, 103,  57, 148,  47,  65, 249, 143,