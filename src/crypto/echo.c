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
		X[0] = (sph_u64)X0 | ((sph_u64)X1 << 32); \
		X[1] = (sph_u64)X2 | ((sph_u64)X3 << 32); \
		if ((K0 = T32(K0 + 1)) == 0) { \
			if ((K1 = T32(K1 + 1)) == 0) \
				if ((K2 = T32(K2 + 1)) == 0) \
					K3 = T32(K3 + 1); \
		} \
	} while (0)

#define BIG_SUB_WORDS   do { \
		AES_2ROUNDS(W[ 0]); \
		AES_2ROUNDS(W[ 1]); \
		AES_2ROUNDS(W[ 2]); \
		AES_2ROUNDS(W[ 3]); \
		AES_2ROUNDS(W[ 4]); \
		AES_2ROUNDS(W[ 5]); \
		AES_2ROUNDS(W[ 6]); \
		AES_2ROUNDS(W[ 7]); \
		AES_2ROUNDS(W[ 8]); \
		AES_2ROUNDS(W[ 9]); \
		AES_2ROUNDS(W[10]); \
		AES_2ROUNDS(W[11]); \
		AES_2ROUNDS(W[12]); \
		AES_2ROUNDS(W[13]); \
		AES_2ROUNDS(W[14]); \
		AES_2ROUNDS(W[15]); \
	} while (0)

#endif

#define SHIFT_ROW1(a, b, c, d)   do { \
		sph_u64 tmp; \
		tmp = W[a][0]; \
		W[a][0] = W[b][0]; \
		W[b][0] = W[c][0]; \
		W[c][0] = W[d][0]; \
		W[d][0] = tmp; \
		tmp = W[a][1]; \
		W[a][1] = W[b][1]; \
		W[b][1] = W[c][1]; \
		W[c][1] = W[d][1]; \
		W[d][1] = tmp; \
	} while (0)

#define SHIFT_ROW2(a, b, c, d)   do { \
		sph_u64 tmp; \
		tmp = W[a][0]; \
		W[a][0] = W[c][0]; \
		W[c][0] = tmp; \
		tmp = W[b][0]; \
		W[b][0] = W[d][0]; \
		W[d][0] = tmp; \
		tmp = W[a][1]; \
		W[a][1] = W[c][1]; \
		W[c][1] = tmp; \
		tmp = W[b][1]; \
		W[b][1] = W[d][1]; \
		W[d][1] = tmp; \
	} while (0)

#define SHIFT_ROW3(a, b, c, d)   SHIFT_ROW1(d, c, b, a)

#define BIG_SHIFT_ROWS   do { \
		SHIFT_ROW1(1, 5, 9, 13); \
		SHIFT_ROW2(2, 6, 10, 14); \
		SHIFT_ROW3(3, 7, 11, 15); \
	} while (0)

#if SPH_SMALL_FOOTPRINT_ECHO

static void
mix_column(sph_u64 W[16][2], int ia, int ib, int ic, int id)
{
	int n;

	for (n = 0; n < 2; n ++) {
		sph_u64 a = W[ia][n];
		sph_u64 b = W[ib][n];
		sph_u64 c = W[ic][n];
		sph_u64 d = W[id][n];
		sph_u64 ab = a ^ b;
		sph_u64 bc = b ^ c;
		sph_u64 cd = c ^ d;
		sph_u64 abx = ((ab & C64(0x8080808080808080)) >> 7) * 27U
			^ ((ab & C64(0x7F7F7F7F7F7F7F7F)) << 1);
		sph_u64 bcx = ((bc & C64(0x8080808080808080)) >> 7) * 27U
			^ ((bc & C64(0x7F7F7F7F7F7F7F7F)) << 1);
		sph_u64 cdx = ((cd & C64(0x8080808080808080)) >> 7) * 27U
			^ ((cd & C64(0x7F7F7F7F7F7F7F7F)) << 1);
		W[ia][n] = abx ^ bc ^ d;
		W[ib][n] = bcx ^ a ^ cd;
		W[ic][n] = cdx ^ ab ^ d;
		W[id][n] = abx ^ bcx ^ cdx ^ ab ^ c;
	}
}

#define MIX_COLUMN(a, b, c, d)   mix_column(W, a, b, c, d)

#else

#define MIX_COLUMN1(ia, ib, ic, id, n)   do { \
		sph_u64 a = W[ia][n]; \
		sph_u64 b = W[ib][n]; \
		sph_u64 c = W[ic][n]; \
		sph_u64 d = W[id][n]; \
		sph_u64 ab = a ^ b; \
		sph_u64 bc = b ^ c; \
		sph_u64 cd = c ^ d; \
		sph_u64 abx = ((ab & C64(0x8080808080808080)) >> 7) * 27U \
			^ ((ab & C64(0x7F7F7F7F7F7F7F7F)) << 1); \
		sph_u64 bcx = ((bc & C64(0x8080808080808080)) >> 7) * 27U \
			^ ((bc & C64(0x7F7F7F7F7F7F7F7F)) << 1); \
		sph_u64 cdx = ((cd & C64(0x8080808080808080)) >> 7) * 27U \
			^ ((cd & C64(0x7F7F7F7F7F7F7F7F)) << 1); \
		W[ia][n] = abx ^ bc ^ d; \
		W[ib][n] = bcx ^ a ^ cd; \
		W[ic][n] = cdx ^ ab ^ d; \
		W[id][n] = abx ^ bcx ^ cdx ^ ab ^ c; \
	} while (0)

#define MIX_COLUMN(a, b, c, d)   do { \
		MIX_COLUMN1(a, b, c, d, 0); \
		MIX_COLUMN1(a, b, c, d, 1); \
	} while (0)

#endif

#define BIG_MIX_COLUMNS   do { \
		MIX_COLUMN(0, 1, 2, 3); \
		MIX_COLUMN(4, 5, 6, 7); \
		MIX_COLUMN(8, 9, 10, 11); \
		MIX_COLUMN(12, 13, 14, 15); \
	} while (0)

#define BIG_ROUND   do { \
		BIG_SUB_WORDS; \
		BIG_SHIFT_ROWS; \
		BIG_MIX_COLUMNS; \
	} while (0)

#define FINAL_SMALL   do { \
		unsigned u; \
		sph_u64 *VV = &sc->u.Vb[0][0]; \
		sph_u64 *WW = &W[0][0]; \
		for (u = 0; u < 8; u ++) { \
			VV[u] ^= sph_dec64le_aligned(sc->buf + (u * 8)) \
				^ sph_dec64le_aligned(sc->buf + (u * 8) + 64) \
				^ sph_dec64le_aligned(sc->buf + (u * 8) + 128) \
				^ WW[u] ^ WW[u + 8] \
				^ WW[u + 16] ^ WW[u + 24]; \
		} \
	} while (0)

#define FINAL_BIG   do { \
		unsigned u; \
		sph_u64 *VV = &sc->u.Vb[0][0]; \
		sph_u64 *WW = &W[0][0]; \
		for (u = 0; u < 16; u ++) { \
			VV[u] ^= sph_dec64le_aligned(sc->buf + (u * 8)) \
				^ WW[u] ^ WW[u + 16]; \
		} \
	} while (0)

#define COMPRESS_SMALL(sc)   do { \
		sph_u32 K0 = sc->C0; \
		sph_u32 K1 = sc->C1; \
		sph_u32 K2 = sc->C2; \
		sph_u32 K3 = sc->C3; \
		unsigned u; \
		INPUT_BLOCK_SMALL(sc); \
		for (u = 0; u < 8; u ++) { \
			BIG_ROUND; \
		} \
		FINAL_SMALL; \
	} while (0)

#define COMPRESS_BIG(sc)   do { \
		sph_u32 K0 = sc->C0; \
		sph_u32 K1 = sc->C1; \
		sph_u32 K2 = sc->C2; \
		sph_u32 K3 = sc->C3; \
		unsigned u; \
		INPUT_BLOCK_BIG(sc); \
		for (u = 0; u < 10; u ++) { \
			BIG_ROUND; \
		} \
		FINAL_BIG; \
	} while (0)

#else

#define DECL_STATE_SMALL   \
	sph_u32 W[16][4];

#define DECL_STATE_BIG   \
	sph_u32 W[16][4];

#define INPUT_BLOCK_SMALL(sc)   do { \
		unsigned u; \
		memcpy(W, sc->u.Vs, 16 * sizeof(sph_u32)); \
		for (u = 0; u < 12; u ++) { \
			W[u + 4][0] = sph_dec32le_aligned( \
				sc->buf + 16 * u); \
			W[u + 4][1] = sph_dec32le_aligned( \
				sc->buf + 16 * u + 4); \
			W[u + 4][2] = sph_dec32le_aligned( \
				sc->buf + 16 * u + 8); \
			W[u + 4][3] = sph_dec32le_aligned( \
				sc->buf + 16 * u + 12); \
		} \
	} while (0)

#define INPUT_BLOCK_BIG(sc)   do { \
		unsigned u; \
		memcpy(W, sc->u.Vs, 32 * sizeof(sph_u32)); \
		for (u = 0; u < 8; u ++) { \
			W[u + 8][0] = sph_dec32le_aligned( \
				sc->buf + 16 * u); \
			W[u + 8][1] = sph_dec32le_aligned( \
				sc->buf + 16 * u + 4); \
			W[u + 8][2] = sph_dec32le_aligned( \
				sc->buf + 16 * u + 8); \
			W[u + 8][3] = sph_dec32le_aligned( \
				sc->buf + 16 * u + 12); \
		} \
	} while (0)

#if SPH_SMALL_FOOTPRINT_ECHO

static void
aes_2rounds_all(sph_u32 W[16][4],
	sph_u32 *pK0, sph_u32 *pK1, sph_u32 *pK2, sph_u32 *pK3)
{
	int n;
	sph_u32 K0 = *pK0;
	sph_u32 K1 = *pK1;
	sph_u32 K2 = *pK2;
	sph_u32 K3 = *pK3;

	for (n = 0; n < 16; n ++) {
		sph_u32 *X = W[n];
		sph_u32 Y0, Y1, Y2, Y3;
		AES_ROUND_LE(X[0], X[1], X[2], X[3],
			K0, K1, K2, K3, Y0, Y1, Y2, Y3);
		AES_ROUND_NOKEY_LE(Y0, Y1, Y2, Y3, X[0], X[1], X[2], X[3]);
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
		sph_u32 Y0, Y1, Y2, Y3; \
		AES_ROUND_LE(X[0], X[1], X[2], X[3], \
			K0, K1, K2, K3, Y0, Y1, Y2, Y3); \
		AES_ROUND_NOKEY_LE(Y0, Y1, Y2, Y3, X[0], X[1], X[2], X[3]); \
		if ((K0 = T32(K0 + 1)) == 0) { \
			if ((K1 = T32(K1 + 1)) == 0) \
				if ((K2 = T32(K2 + 1)) == 0) \
					K3 = T32(K3 + 1); \
		} \
	} while (0)

#define BIG_SUB_WORDS   do { \
		AES_2ROUNDS(W[ 0]); \
		AES_2ROUNDS(W[ 1]); \
		AES_2ROUNDS(W[ 2]); \
		AES_2ROUNDS(W[ 3]); \
		AES_2ROUNDS(W[ 4]); \
		AES_2ROUNDS(W[ 5]); \
		AES_2ROUNDS(W[ 6]); \
		AES_2ROUNDS(W[ 7]); \
		AES_2ROUNDS(W[ 8]); \
		AES_2ROUNDS(W[ 9]); \
		AES_2ROUNDS(W[10]); \
		AES_2ROUNDS(W[11]); \
		AES_2ROUNDS(W[12]); \
		AES_2ROUNDS(W[13]); \
		AES_2ROUNDS(W[14]); \
		AES_2ROUNDS(W[15]); \
	} while (0)

#endif

#define SHIFT_ROW1(a, b, c, d)   do { \
		sph_u32 tmp; \
		tmp = W[a][0]; \
		W[a][0] = W[b][0]; \
		W[b][0] = W[c][0]; \
		W[c][0] = W[d][0]; \
		W[d][0] = tmp; \
		tmp = W[a][1]; \
		W[a][1] = W[b][1]; \
		W[b][1] = W[c][1]; \
		W[c][1] = W[d][1]; \
		W[d][1] = tmp; \
		tmp = W[a][2]; \
		W[a][2] = W[b][2]; \
		W[b][2] = W[c][2]; \
		W[c][2] = W[d][2]; \
		W[d][2] = tmp; \
		tmp = W[a][3]; \
		W[a][3] = W[b][3]; \
		W[b][3] = W[c][3]; \
		W[c][3] = W[d][3]; \
		W[d][3] = tmp; \
	} while (0)

#define SHIFT_ROW2(a, b, c, d)   do { \
		sph_u32 tmp; \
		tmp = W[a][0]; \
		W[a][0] = W[c][0]; \
		W[c][0] = tmp; \
		tmp = W[b][0]; \
		W[b][0] = W[d][0]; \
		W[d][0] = tmp; \
		tmp = W[a][1]; \
		W[a][1] = W[c][1]; \
		W[c][1] = tmp; \
		tmp = W[b][1]; \
		W[b][1] = W[d][1]; \
		W[d][1] = tmp; \
		tmp = W[a][2]; \
		W[a][2] = W[c][2]; \
		W[c][2] = tmp; \
		tmp = W[b][2]; \
		W[b][2] = W[d][2]; \
		W[d][2] = tmp; \
		tmp = W[a][3]; \
		W[a][3] = W[c][3]; \
		W[c][3] = tmp; \
		tmp = W[b][3]; \
		W[b][3] = W[d][3]; \
		W[d][3] = tmp; \
	} while (0)

#define SHIFT_ROW3(a, b, c, d)   SHIFT_ROW1(d, c, b, a)

#define BIG_SHIFT_ROWS   do { \
		SHIFT_ROW1(1, 5, 9, 13); \
		SHIFT_ROW2(2, 6, 10, 14); \
		SHIFT_ROW3(3, 7, 11, 15); \
	} while (0)

#if SPH_SMALL_FOOTPRINT_ECHO

static void
mix_column(sph_u32 W[16][4], int ia, int ib, int ic, int id)
{
	int n;

	for (n = 0; n < 4; n ++) {
		sph_u32 a = W[ia][n];
		sph_u32 b = W[ib][n];
		sph_u32 c = W[ic][n];
		sph_u32 d = W[id][n];
		sph_u32 ab = a ^ b;
		sph_u32 bc = b ^ c;
		sph_u32 cd = c ^ d;
		sph_u32 abx = ((ab & C32(0x80808080)) >> 7) * 27U
			^ ((ab & C32(0x7F7F7F7F)) << 1);
		sph_u32 bcx = ((bc & C32(0x80808080)) >> 7) * 27U
			^ ((bc & C32(0x7F7F7F7F)) << 1);
		sph_u32 cdx = ((cd & C32(0x80808080)) >> 7) * 27U
			^ ((cd & C32(0x7F7F7F7F)) << 1);
		W[ia][n] = abx ^ bc ^ d;
		W[ib][n] = bcx ^ a ^ cd;
		W[ic][n] = cdx ^ ab ^ d;
		W[id][n] = abx ^ bcx ^ cdx ^ ab ^ c;
	}
}

#define MIX_COLUMN(a, b, c, d)   mix_column(W, a, b, c, d)

#else

#define MIX_COLUMN1(ia, ib, ic, id, n)   do { \
		sph_u32 a = W[ia][n]; \
		sph_u32 b = W[ib][n]; \
		sph_u32 c = W[ic][n]; \
		sph_u32 d = W[id][n]; \
		sph_u32 ab = a ^ b; \
		sph_u32 bc = b ^ c; \
		sph_u32 cd = c ^ d; \
		sph_u32 abx = ((ab & C32(0x80808080)) >> 7) * 27U \
			^ ((ab & C32(0x7F7F7F7F)) << 1); \
		sph_u32 bcx = ((bc & C32(0x80808080)) >> 7) * 27U \
			^ ((bc & C32(0x7F7F7F7F)) << 1); \
		sph_u32 cdx = ((cd & C32(0x80808080)) >> 7) * 27U \
			^ ((cd & C32(0x7F7F7F7F)) << 1); \
		W[ia][n] = abx ^ bc ^ d; \
		W[ib][n] = bcx ^ a ^ cd; \
		W[ic][n] = cdx ^ ab ^ d; \
		W[id][n] = abx ^ bcx ^ cdx ^ ab ^ c; \
	} while (0)

#define MIX_COLUMN(a, b, c, d)   do { \
		MIX_COLUMN1(a, b, c, d, 0); \
		MIX_COLUMN1(a, b, c, d, 1); \
		MIX_COLUMN1(a, b, c, d, 2); \
		MIX_COLUMN1(a, b, c, d, 3); \
	} while (0)

#endif

#define BIG_MIX_COLUMNS   do { \
		MIX_COLUMN(0, 1, 2, 3); \
		MIX_COLUMN(4, 5, 6, 7); \
		MIX_COLUMN(8, 9, 10, 11); \
		MIX_COLUMN(12, 13, 14, 15); \
	} while (0)

#define BIG_R