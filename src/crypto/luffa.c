/* $Id: luffa.c 219 2010-06-08 17:24:41Z tp $ */
/*
 * Luffa implementation.
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

#include "sph_luffa.h"

#ifdef __cplusplus
extern "C"{
#endif

#if SPH_64_TRUE && !defined SPH_LUFFA_PARALLEL
#define SPH_LUFFA_PARALLEL   1
#endif

#ifdef _MSC_VER
#pragma warning (disable: 4146)
#endif

static const sph_u32 V_INIT[5][8] = {
	{
		SPH_C32(0x6d251e69), SPH_C32(0x44b051e0),
		SPH_C32(0x4eaa6fb4), SPH_C32(0xdbf78465),
		SPH_C32(0x6e292011), SPH_C32(0x90152df4),
		SPH_C32(0xee058139), SPH_C32(0xdef610bb)
	}, {
		SPH_C32(0xc3b44b95), SPH_C32(0xd9d2f256),
		SPH_C32(0x70eee9a0), SPH_C32(0xde099fa3),
		SPH_C32(0x5d9b0557), SPH_C32(0x8fc944b3),
		SPH_C32(0xcf1ccf0e), SPH_C32(0x746cd581)
	}, {
		SPH_C32(0xf7efc89d), SPH_C32(0x5dba5781),
		SPH_C32(0x04016ce5), SPH_C32(0xad659c05),
		SPH_C32(0x0306194f), SPH_C32(0x666d1836),
		SPH_C32(0x24aa230a), SPH_C32(0x8b264ae7)
	}, {
		SPH_C32(0x858075d5), SPH_C32(0x36d79cce),
		SPH_C32(0xe571f7d7), SPH_C32(0x204b1f67),
		SPH_C32(0x35870c6a), SPH_C32(0x57e9e923),
		SPH_C32(0x14bcb808), SPH_C32(0x7cde72ce)
	}, {
		SPH_C32(0x6c68e9be), SPH_C32(0x5ec41e22),
		SPH_C32(0xc825b7c7), SPH_C32(0xaffb4363),
		SPH_C32(0xf5df3999), SPH_C32(0x0fc688f1),
		SPH_C32(0xb07224cc), SPH_C32(0x03e86cea)
	}
};

static const sph_u32 RC00[8] = {
	SPH_C32(0x303994a6), SPH_C32(0xc0e65299),
	SPH_C32(0x6cc33a12), SPH_C32(0xdc56983e),
	SPH_C32(0x1e00108f), SPH_C32(0x7800423d),
	SPH_C32(0x8f5b7882), SPH_C32(0x96e1db12)
};

static const sph_u32 RC04[8] = {
	SPH_C32(0xe0337818), SPH_C32(0x441ba90d),
	SPH_C32(0x7f34d442), SPH_C32(0x9389217f),
	SPH_C32(0xe5a8bce6), SPH_C32(0x5274baf4),
	SPH_C32(0x26889ba7), SPH_C32(0x9a226e9d)
};

static const sph_u32 RC10[8] = {
	SPH_C32(0xb6de10ed), SPH_C32(0x70f47aae),
	SPH_C32(0x0707a3d4), SPH_C32(0x1c1e8f51),
	SPH_C32(0x707a3d45), SPH_C32(0xaeb28562),
	SPH_C32(0xbaca1589), SPH_C32(0x40a46f3e)
};

static const sph_u32 RC14[8] = {
	SPH_C32(0x01685f3d), SPH_C32(0x05a17cf4),
	SPH_C32(0xbd09caca), SPH_C32(0xf4272b28),
	SPH_C32(0x144ae5cc), SPH_C32(0xfaa7ae2b),
	SPH_C32(