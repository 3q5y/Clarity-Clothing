/* $Id: echo.c 227 2010-06-16 17:28:38Z tp $ */
/*
 * ECHO implementation.
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

#include "sph_echo.h"

#ifdef __cplusplus
extern "C"{
#endif

#if SPH_SMALL_FOOTPRINT && !defined SPH_SMALL_FOOTPRINT_ECHO
#define SPH_SMALL_FOOTPRINT_ECHO   1
#endif

/*
 * Some measures tend to show that the 64-bit implementation offers
 * better performance only on a "64-bit architectures", those which have
 * actual 64-bit registers.
 */
#if !defined SPH_ECHO_64 && SPH_64_TRUE
#define SPH_ECHO_64   1
#endif

/*
 * We can use a 64-bit implementation only if a 64-bit type is available.
 */
#if !SPH_64
#undef SPH_ECHO_64
#endif

#ifdef _MSC_VER
#pragma warning (disable: 4146)
#endif

#define T32   SPH_T32
#define C32   SPH_C32
#if SPH_64
#define C64   SPH_C64
#endif

#define AES_BIG_ENDIAN   0
#include "aes_helper.c"

#if SPH_ECHO_64

#define DECL_STATE_SMALL   \
	sph_u64 W[16][2];

#define DECL_STATE_BIG   \
	sph_u64 W[16][2];

#define INPUT_BLOCK_SMALL(sc)   do { \
		unsigned u; \
		memcpy(W, sc->u.Vb, 8 * sizeof(sph_u64)); \
		for (u = 0; u < 12; u ++) { \
			W[u + 4][0] = sph_dec64le_aligned( \
				sc->buf + 16 * u); \
			W[u + 4][1] = sph_dec64le_aligned( \
				sc->buf + 16 * u + 8); \
		} \
	} while (0)

#define INPUT_BLOCK_BIG(sc)   do { \
		unsigned u; \
		memcpy(W, sc->u.Vb, 16 * sizeof(sph_u64)); \
		for (u = 0; u < 8; u ++) { \
			W[u + 8][0] = sph_dec64le_aligned( \
				sc->buf + 16 * u); \
			W[u + 8][1] = sph_dec64le_aligned( \
				sc->buf + 16 * u + 8); \
		} \
	} while (0)

#if SPH_SMALL_FOOTPRINT_ECHO

static void
aes_2rounds_all(sph_u64 W[16][2],
	sph_u32 *pK0, sph_u32 *pK1, sph_u32 *pK2, sph_u32 *pK3)
{
	int n;
	sph_u32 K0 = *pK0;
	sph_u32 K1 = *pK1;
	sph_u32 K2 = *pK2;
	sph_u32 K3 = *pK3;

	for (n = 0; n < 16; n ++) {
		sph_u64 Wl = W[n][0];
		sph_u64 Wh = W[n][1];
		sph_u32 X0 = (sph_u32)Wl;
		sph_u32 X1 = (sph_u32)(Wl >> 32);
		sph_u32 X2 = (sph_u32)Wh;
		sph_u32 X3 = (sph_u32)(Wh >> 32);
		sph_u32 Y0, Y1, Y2, Y3; \
		AES_ROUND_LE(X0, X1, X2, X3, K0, K1, K2, K3, Y0, Y1, Y2, Y3);
		AES_ROUND_NOKEY_LE(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
		W[n][0] = (sph_u64)X0 | ((sph_u64)X1 << 32);
		W[n][1] = (sph_u64)X2 | ((sph_u64)X3 << 32);
		if ((K0 = T32(K0 + 1)) == 0) {
			if ((K1 = T32(K1 + 1)) == 0)
				if ((K2 = T32(K2 + 1)) == 0)
					K3 = T32(K3 + 1);
		}
	}
	*pK0 = K0;
	*pK1 = K1;
	*pK2 = K2;
	*pK3 = K3;
}

#define BIG_SUB_WORDS   do { \
		aes_2rounds_all(W, &K0, &K1, &K2, &K3); \
	} while (0)

#else

#define AES_2ROUNDS(X)   do { \
		sph_u32 X0 = (sph_u32)(X[0]); \
		sph_u32 X1 = (sph_u32)(X[0] >> 32); \
		sph_u32 X2 = (sph_u32)(X[1]); \
		sph_u32 X3 = (sph_u32)(X[1] >> 32); \
		sph_u32 Y0, Y1, Y2, Y3; \
		AES_ROUND_LE(X0, X1, X2, X3, K0, K1, K2, K3, Y0, Y1, Y2, Y3); \
		AES_ROUND_NOKEY_LE(Y0, Y1, Y2, Y3, X0, X1, X2, X3); \
		X[0] = (sph_u64)X0 | ((sph_u6