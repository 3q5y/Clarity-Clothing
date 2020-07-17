/* $Id: groestl.c 260 2011-07-21 01:02:38Z tp $ */
/*
 * Groestl implementation.
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

#include "sph_groestl.h"

#ifdef __cplusplus
extern "C"{
#endif

#if SPH_SMALL_FOOTPRINT && !defined SPH_SMALL_FOOTPRINT_GROESTL
#define SPH_SMALL_FOOTPRINT_GROESTL   1
#endif

/*
 * Apparently, the 32-bit-only version is not faster than the 64-bit
 * version unless using the "small footprint" code on a 32-bit machine.
 */
#if !defined SPH_GROESTL_64
#if SPH_SMALL_FOOTPRINT_GROESTL && !SPH_64_TRUE
#define SPH_GROESTL_64   0
#else
#define SPH_GROESTL_64   1
#endif
#endif

#if !SPH_64
#undef SPH_GROESTL_64
#endif

#ifdef _MSC_VER
#pragma warning (disable: 4146)
#endif

/*
 * The internal representation may use either big-endian or
 * little-endian. Using the platform default representation speeds up
 * encoding and decoding between bytes and the matrix columns.
 */

#undef USE_LE
#if SPH_GROESTL_LITTLE_ENDIAN
#define USE_LE   1
#elif SPH_GROESTL_BIG_ENDIAN
#define USE_LE   0
#elif SPH_LITTLE_ENDIAN
#define USE_LE   1
#endif

#if USE_LE

#define C32e(x)     ((SPH_C32(x) >> 24) \
                    | ((SPH_C32(x) >>  8) & SPH_C32(0x0000FF00)) \
                    | ((SPH_C32(x) <<  8) & SPH_C32(0x00FF0000)) \
                    | ((SPH_C32(x) << 24) & SPH_C32(0xFF000000)))
#define dec32e_aligned   sph_dec32le_aligned
#define enc32e           sph_enc32le
#define B32_0(x)    ((x) & 0xFF)
#define B32_1(x)    (((x) >> 8) & 0xFF)
#define B32_2(x)    (((x) >> 16) & 0xFF)
#define B32_3(x)    ((x) >> 24)

#define R32u(u, d)   SPH_T32(((u) << 16) | ((d) >> 16))
#define R32d(u, d)   SPH_T32(((u) >> 16) | ((d) << 16))

#define PC32up(j, r)   ((sph_u32)((j) + (r)))
#define PC32dn(j, r)   0
#define QC32up(j, r)   SPH_C32(0xFFFFFFFF)
#define QC32dn(j, r)   (((sph_u32)(r) << 24) ^ SPH_T32(~((sph_u32)(j) << 24)))

#if SPH_64
#define C64e(x)     ((SPH_C64(x) >> 56) \
                    | ((SPH_C64(x) >> 40) & SPH_C64(0x000000000000FF00)) \
                    | ((SPH_C64(x) >> 24) & SPH_C64(0x0000000000FF0000)) \
                    | ((SPH_C64(x) >>  8) & SPH_C64(0x00000000FF000000)) \
                    | ((SPH_C64(x) <<  8) & SPH_C64(0x000000FF00000000)) \
                    | ((SPH_C64(x) << 24) & SPH_C64(0x0000FF0000000000)) \
                    | ((SPH_C64(x) << 40) & SPH_C64(0x00FF000000000000)) \
                    | ((SPH_C64(x) << 56) & SPH_C64(0xFF00000000000000)))
#define dec64e_aligned   sph_dec64le_aligned
#define enc64e           sph_enc64le
#define B64_0(x)    ((x) & 0xFF)
#define B64_1(x)    (((x) >> 8) & 0xFF)
#define B64_2(x)    (((x) >> 16) & 0xFF)
#define B64_3(x)    (((x) >> 24) & 0xFF)
#define B64_4(x)    (((x) >> 32) & 0xFF)
#define B64_5(x)    (((x) >> 40) & 0xFF)
#define B64_6(x)    (((x) >> 48) & 0xFF)
#define B64_7(x)    ((x) >> 56)
#define R64         SPH_ROTL64
#define PC64(j, r)  ((sph_u64)((j) + (r)))
#define QC64(j, r)  (((sph_u64)(r) << 56) ^ SPH_T64(~((sph_u64)(j) << 56)))
#endif

#else

#define C32e(x)     SPH_C32(x)
#define dec32e_aligned   sph_dec32be_aligned
#define enc32e           sph_enc32be
#define B32_0(x)    ((x) >> 24)
#define B32_1(x)    (((x) >> 16) & 0xFF)
#define B32_2(x)    (((x) >> 8) & 0xFF)
#define B32_3(x)    ((x) & 0xFF)

#define R32u(u, d)   SPH_T32(((u) >> 16) | ((d) << 16))
#define R32d(u, d)   SPH_T32(((u) << 16) | ((d) >> 16))

#define PC32up(j, r)   ((sph_u32)((j) + (r)) << 24)
#define PC32dn(j, r)   0
#define QC32up(j, r)   SPH_C32(0xFFFFFFFF)
#define QC32dn(j, r)   ((sph_u32)(r) ^ SPH_T32(~(sph_u32)(j)))

#if SPH_64
#define C64e(x)     SPH_C64(x)
#define dec64e_aligned   sph_dec64be_aligned
#define enc64e           sph_enc64be
#define B64_0(x)    ((x) >> 56)
#define B64_1(x)    (((x) >> 48) & 0xFF)
#define B64_2(x)    (((x) >> 40) & 0xFF)
#define B64_3(x)    (((x) >> 32) & 0xFF)
#define B64_4(x)    (((x) >> 24) & 0xFF)
#define B64_5(x)    (((x) >> 16) & 0xFF)
#define B64_6(x)    (((x) >> 8) & 0xFF)
#define B64_7(x)    ((x) & 0xFF)
#define R64         SPH_ROTR64
#define PC64(j, r)  ((sph_u64)((j) + (r)) << 56)
#define QC64(j, r)  ((sph_u64)(r) ^ SPH_T64(~(sph_u64)(j)))
#endif

#endif

#if SPH_GROESTL_64

static const sph_u64 T0[] = {
	C64e(0xc632f4a5f497a5c6), C64e(0xf86f978497eb84f8),
	C64e(0xee5eb099b0c799ee), C64e(0xf67a8c8d8cf78df6),
	C64e(0xffe8170d17e50dff), C64e(0xd60adcbddcb7bdd6),
	C64e(0xde16c8b1c8a7b1de), C64e(0x916dfc54fc395491),
	C64e(0x6090f050f0c05060), C64e(0x0207050305040302),
	C64e(0xce2ee0a9e087a9ce), C64e(0x56d1877d87ac7d56),
	C64e(0xe7cc2b192bd519e7), C64e(0xb513a662a67162b5),
	C64e(0x4d7c31e6319ae64d), C64e(0xec59b59ab5c39aec),
	C64e(0x8f40cf45cf05458f), C64e(0x1fa3bc9dbc3e9d1f),
	C64e(0x8949c040c0094089), C64e(0xfa68928792ef87fa),
	C64e(0xefd03f153fc515ef), C64e(0xb29426eb267febb2),
	C64e(0x8ece40c94007c98e), C64e(0xfbe61d0b1ded0bfb),
	C64e(0x416e2fec2f82ec41), C64e(0xb31aa967a97d67b3),
	C64e(0x5f431cfd1cbefd5f), C64e(0x456025ea258aea45),
	C64e(0x23f9dabfda46bf23), C64e(0x535102f702a6f753),
	C64e(0xe445a196a1d396e4), C64e(0x9b76ed5bed2d5b9b),
	C64e(0x75285dc25deac275), C64e(0xe1c5241c24d91ce1),
	C64e(0x3dd4e9aee97aae3d), C64e(0x4cf2be6abe986a4c),
	C64e(0x6c82ee5aeed85a6c), C64e(0x7ebdc341c3fc417e),
	C64e(0xf5f3060206f102f5), C64e(0x8352d14fd11d4f83),
	C64e(0x688ce45ce4d05c68), C64e(0x515607f407a2f451),
	C64e(0xd18d5c345cb934d1), C64e(0xf9e1180818e908f9),
	C64e(0xe24cae93aedf93e2), C64e(0xab3e9573954d73ab),
	C64e(0x6297f553f5c45362), C64e(0x2a6b413f41543f2a),
	C64e(0x081c140c14100c08), C64e(0x9563f652f6315295),
	C64e(0x46e9af65af8c6546), C64e(0x9d7fe25ee2215e9d),
	C64e(0x3048782878602830), C64e(0x37cff8a1f86ea137),
	C64e(0x0a1b110f11140f0a), C64e(0x2febc4b5c45eb52f),
	C64e(0x0e151b091b1c090e), C64e(0x247e5a365a483624),
	C64e(0x1badb69bb6369b1b), C64e(0xdf98473d47a53ddf),
	C64e(0xcda76a266a8126cd), C64e(0x4ef5bb69bb9c694e),
	C64e(0x7f334ccd4cfecd7f), C64e(0xea50ba9fbacf9fea),
	C64e(0x123f2d1b2d241b12), C64e(0x1da4b99eb93a9e1d),
	C64e(0x58c49c749cb07458), C64e(0x3446722e72682e34),
	C64e(0x3641772d776c2d36), C64e(0xdc11cdb2cda3b2dc),
	C64e(0xb49d29ee2973eeb4), C64e(0x5b4d16fb16b6fb5b),
	C64e(0xa4a501f60153f6a4), C64e(0x76a1d74dd7ec4d76),
	C64e(0xb714a361a37561b7), C64e(0x7d3449ce49face7d),
	C64e(0x52df8d7b8da47b52), C64e(0xdd9f423e42a13edd),
	C64e(0x5ecd937193bc715e), C64e(0x13b1a297a2269713),
	C64e(0xa6a204f50457f5a6), C64e(0xb901b868b86968b9),
	C64e(0x0000000000000000), C64e(0xc1b5742c74992cc1),
	C64e(0x40e0a060a0806040), C64e(0xe3c2211f21dd1fe3),
	C64e(0x793a43c843f2c879), C64e(0xb69a2ced2c77edb6),
	C64e(0xd40dd9bed9b3bed4), C64e(0x8d47ca46ca01468d),
	C64e(0x671770d970ced967), C64e(0x72afdd4bdde44b72),
	C64e(0x94ed79de7933de94), C64e(0x98ff67d4672bd498),
	C64e(0xb09323e8237be8b0), C64e(0x855bde4ade114a85),
	C64e(0xbb06bd6bbd6d6bbb), C64e(0xc5bb7e2a7e912ac5),
	C64e(0x4f7b34e5349ee54f), C64e(0xedd73a163ac116ed),
	C64e(0x86d254c55417c586), C64e(0x9af862d7622fd79a),
	C64e(0x6699ff55ffcc5566), C64e(0x11b6a794a7229411),
	C64e(0x8ac04acf4a0fcf8a), C64e(0xe9d9301030c910e9),
	C64e(0x040e0a060a080604), C64e(0xfe66988198e781fe),
	C64e(0xa0ab0bf00b5bf0a0), C64e(0x78b4cc44ccf04478),
	C64e(0x25f0d5bad54aba25), C64e(0x4b753ee33e96e34b),
	C64e(0xa2ac0ef30e5ff3a2), C64e(0x5d4419fe19bafe5d),
	C64e(0x80db5bc05b1bc080), C64e(0x0580858a850a8a05),
	C64e(0x3fd3ecadec7ead3f), C64e(0x21fedfbcdf42bc21),
	C64e(0x70a8d848d8e04870), C64e(0xf1fd0c040cf904f1),
	C64e(0x63197adf7ac6df63), C64e(0x772f58c158eec177),
	C64e(0xaf309f759f4575af), C64e(0x42e7a563a5846342),
	C64e(0x2070503050403020), C64e(0xe5cb2e1a2ed11ae5),
	C64e(0xfdef120e12e10efd), C64e(0xbf08b76db7656dbf),
	C64e(0x8155d44cd4194c81), C64e(0x18243c143c301418),
	C64e(0x26795f355f4c3526), C64e(0xc3b2712f719d2fc3),
	C64e(0xbe8638e13867e1be), C64e(0x35c8fda2fd6aa235),
	C64e(0x88c74fcc4f0bcc88), C64e(0x2e654b394b5c392e),
	C64e(0x936af957f93d5793), C64e(0x55580df20daaf255),
	C64e(0xfc619d829de382fc), C64e(0x7ab3c947c9f4477a),
	C64e(0xc827efacef8bacc8), C64e(0xba8832e7326fe7ba),
	C64e(0x324f7d2b7d642b32), C64e(0xe642a495a4d795e6),
	C64e(0xc03bfba0fb9ba0c0), C64e(0x19aab398b3329819),
	C64e(0x9ef668d16827d19e), C64e(0xa322817f815d7fa3),
	C64e(0x44eeaa66aa886644), C64e(0x54d6827e82a87e54),
	C64e(0x3bdde6abe676ab3b), C64e(0x0b959e839e16830b),
	C64e(0x8cc945ca4503ca8c), C64e(0xc7bc7b297b9529c7),
	C64e(0x6b056ed36ed6d36b), C64e(0x286c443c44503c28),
	C64e(0xa72c8b798b5579a7), C64e(0xbc813de23d63e2bc),
	C64e(0x1631271d272c1d16), C64e(0xad379a769a4176ad),
	C64e(0xdb964d3b4dad3bdb), C64e(0x649efa56fac85664),
	C64e(0x74a6d24ed2e84e74), C64e(0x1436221e22281e14),
	C64e(0x92e476db763fdb92), C64e(0x0c121e0a1e180a0c),
	C64e(0x48fcb46cb4906c48), C64e(0xb88f37e4376be4b8),
	C64e(0x9f78e75de7255d9f), C64e(0xbd0fb26eb2616ebd),
	C64e(0x43692aef2a86ef43), C64e(0xc435f1a6f193a6c4),
	C64e(0x39dae3a8e372a839), C64e(0x31c6f7a4f762a431),
	C64e(0xd38a593759bd37d3), C64e(0xf274868b86ff8bf2),
	C64e(0xd583563256b132d5), C64e(0x8b4ec543c50d438b),
	C64e(0x6e85eb59ebdc596e), C64e(0xda18c2b7c2afb7da),
	C64e(0x018e8f8c8f028c01), C64e(0xb11dac64ac7964b1),
	C64e(0x9cf16dd26d23d29c), C64e(0x49723be03b92e049),
	C64e(0xd81fc7b4c7abb4d8), C64e(0xacb915fa1543faac),
	C64e(0xf3fa090709fd07f3), C64e(0xcfa06f256f8525cf),
	C64e(0xca20eaafea8fafca), C64e(0xf47d898e89f38ef4),
	C64e(0x476720e9208ee947), C64e(0x1038281828201810),
	C64e(0x6f0b64d564ded56f), C64e(0xf073838883fb88f0),
	C64e(0x4afbb16fb1946f4a), C64e(0x5cca967296b8725c),
	C64e(0x38546c246c702438), C64e(0x575f08f108aef157),
	C64e(0x732152c752e6c773), C64e(0x9764f351f3355197),
	C64e(0xcbae6523658d23cb), C64e(0xa125847c84597ca1),
	C64e(0xe857bf9cbfcb9ce8), C64e(0x3e5d6321637c213e),
	C64e(0x96ea7cdd7c37dd96), C64e(0x611e7fdc7fc2dc61),
	C64e(0x0d9c9186911a860d), C64e(0x0f9b9485941e850f),
	C64e(0xe04bab90abdb90e0), C64e(0x7cbac642c6f8427c),
	C64e(0x712657c457e2c471), C64e(0xcc29e5aae583aacc),
	C64e(0x90e373d8733bd890), C64e(0x06090f050f0c0506),
	C64e(0xf7f4030103f501f7), C64e(0x1c2a36123638121c),
	C64e(0xc23cfea3fe9fa3c2), C64e(0x6a8be15fe1d45f6a),
	C64e(0xaebe10f91047f9ae), C64e(0x69026bd06bd2d069),
	C64e(0x17bfa891a82e9117), C64e(0x9971e858e8295899),
	C64e(0x3a5369276974273a), C64e(0x27f7d0b9d04eb927),
	C64e(0xd991483848a938d9), C64e(0xebde351335cd13eb),
	C64e(0x2be5ceb3ce56b32b), C64e(0x2277553355443322),
	C64e(0xd204d6bbd6bfbbd2), C64e(0xa9399070904970a9),
	C64e(0x07878089800e8907), C64e(0x33c1f2a7f266a733),
	C64e(0x2decc1b6c15ab62d), C64e(0x3c5a66226678223c),
	C64e(0x15b8ad92ad2a9215), C64e(0xc9a96020608920c9),
	C64e(0x875cdb49db154987), C64e(0xaab01aff1a4fffaa),
	C64e(0x50d8887888a07850), C64e(0xa52b8e7a8e517aa5),
	C64e(0x03898a8f8a068f03), C64e(0x594a13f813b2f859),
	C64e(0x09929b809b128009), C64e(0x1a2339173934171a),
	C64e(0x651075da75cada65), C64e(0xd784533153b531d7),
	C64e(0x84d551c65113c684), C64e(0xd003d3b8d3bbb8d0),
	C64e(0x82dc5ec35e1fc382), C64e(0x29e2cbb0cb52b029),
	C64e(0x5ac3997799b4775a), C64e(0x1e2d3311333c111e),
	C64e(0x7b3d46cb46f6cb7b), C64e(0xa8b71ffc1f4bfca8),
	C64e(0x6d0c61d661dad66d), C64e(0x2c624e3a4e583a2c)
};

#if !SPH_SMALL_FOOTPRINT_GROESTL

static const sph_u64 T1[] = {
	C64e(0xc6c632f4a5f497a5), C64e(0xf8f86f978497eb84),
	C64e(0xeeee5eb099b0c799), C64e(0xf6f67a8c8d8cf78d),
	C64e(0xffffe8170d17e50d), C64e(0xd6d60adcbddcb7bd),
	C64e(0xdede16c8b1c8a7b1), C64e(0x91916dfc54fc3954),
	C64e(0x606090f050f0c050), C64e(0x0202070503050403),
	C64e(0xcece2ee0a9e087a9), C64e(0x5656d1877d87ac7d),
	C64e(0xe7e7cc2b192bd519), C64e(0xb5b513a662a67162),
	C64e(0x4d4d7c31e6319ae6), C64e(0xecec59b59ab5c39a),
	C64e(0x8f8f40cf45cf0545), C64e(0x1f1fa3bc9dbc3e9d),
	C64e(0x898949c040c00940), C64e(0xfafa68928792ef87),
	C64e(0xefefd03f153fc515), C64e(0xb2b29426eb267feb),
	C64e(0x8e8ece40c94007c9), C64e(0xfbfbe61d0b1ded0b),
	C64e(0x41416e2fec2f82ec), C64e(0xb3b31aa967a97d67),
	C64e(0x5f5f431cfd1cbefd), C64e(0x45456025ea258aea),
	C64e(0x2323f9dabfda46bf), C64e(0x53535102f702a6f7),
	C64e(0xe4e445a196a1d396), C64e(0x9b9b76ed5bed2d5b),
	C64e(0x7575285dc25deac2), C64e(0xe1e1c5241c24d91c),
	C64e(0x3d3dd4e9aee97aae), C64e(0x4c4cf2be6abe986a),
	C64e(0x6c6c82ee5aeed85a), C64e(0x7e7ebdc341c3fc41),
	C64e(0xf5f5f3060206f102), C64e(0x838352d14fd11d4f),
	C64e(0x68688ce45ce4d05c), C64e(0x51515607f407a2f4),
	C64e(0xd1d18d5c345cb934), C64e(0xf9f9e1180818e908),
	C64e(0xe2e24cae93aedf93), C64e(0xabab3e9573954d73),
	C64e(0x626297f553f5c453), C64e(0x2a2a6b413f41543f),
	C64e(0x08081c140c14100c), C64e(0x959563f652f63152),
	C64e(0x4646e9af65af8c65), C64e(0x9d9d7fe25ee2215e),
	C64e(0x3030487828786028), C64e(0x3737cff8a1f86ea1),
	C64e(0x0a0a1b110f11140f), C64e(0x2f2febc4b5c45eb5),
	C64e(0x0e0e151b091b1c09), C64e(0x24247e5a365a4836),
	C64e(0x1b1badb69bb6369b), C64e(0xdfdf98473d47a53d),
	C64e(0xcdcda76a266a8126), C64e(0x4e4ef5bb69bb9c69),
	C64e(0x7f7f334ccd4cfecd), C64e(0xeaea50ba9fbacf9f),
	C64e(0x12123f2d1b2d241b), C64e(0x1d1da4b99eb93a9e),
	C64e(0x5858c49c749cb074), C64e(0x343446722e72682e),
	C64e(0x363641772d776c2d), C64e(0xdcdc11cdb2cda3b2),
	C64e(0xb4b49d29ee2973ee), C64e(0x5b5b4d16fb16b6fb),
	C64e(0xa4a4a501f60153f6), C64e(0x7676a1d74dd7ec4d),
	C64e(0xb7b714a361a37561), C64e(0x7d7d3449ce49face),
	C64e(0x5252df8d7b8da47b), C64e(0xdddd9f423e42a13e),
	C64e(0x5e5ecd937193bc71), C64e(0x1313b1a297a22697),
	C64e(0xa6a6a204f50457f5), C64e(0xb9b901b868b86968),
	C64e(0x0000000000000000), C64e(0xc1c1b5742c74992c),
	C64e(0x4040e0a060a08060), C64e(0xe3e3c2211f21dd1f),
	C64e(0x79793a43c843f2c8), C64e(0xb6b69a2ced2c77ed),
	C64e(0xd4d40dd9bed9b3be), C64e(0x8d8d47ca46ca0146),
	C64e(0x67671770d970ced9), C64e(0x7272afdd4bdde44b),
	C64e(0x9494ed79de7933de), C64e(0x9898ff67d4672bd4),
	C64e(0xb0b09323e8237be8), C64e(0x85855bde4ade114a),
	C64e(0xbbbb06bd6bbd6d6b), C64e(0xc5c5bb7e2a7e912a),
	C64e(0x4f4f7b34e5349ee5), C64e(0xededd73a163ac116),
	C64e(0x8686d254c55417c5), C64e(0x9a9af862d7622fd7),
	C64e(0x666699ff55ffcc55), C64e(0x1111b6a794a72294),
	C64e(0x8a8ac04acf4a0fcf), C64e(0xe9e9d9301030c910),
	C64e(0x04040e0a060a0806), C64e(0xfefe66988198e781),
	C64e(0xa0a0ab0bf00b5bf0), C64e(0x7878b4cc44ccf044),
	C64e(0x2525f0d5bad54aba), C64e(0x4b4b753ee33e96e3),
	C64e(0xa2a2ac0ef30e5ff3), C64e(0x5d5d4419fe19bafe),
	C64e(0x8080db5bc05b1bc0), C64e(0x050580858a850a8a),
	C64e(0x3f3fd3ecadec7ead), C64e(0x2121fedfbcdf42bc),
	C64e(0x7070a8d848d8e048), C64e(0xf1f1fd0c040cf904),
	C64e(0x6363197adf7ac6df), C64e(0x77772f58c158eec1),
	C64e(0xafaf309f759f4575), C64e(0x4242e7a563a58463),
	C64e(0x2020705030504030), C64e(0xe5e5cb2e1a2ed11a),
	C64e(0xfdfdef120e12e10e), C64e(0xbfbf08b76db7656d),
	C64e(0x818155d44cd4194c), C64e(0x1818243c143c3014),
	C64e(0x2626795f355f4c35), C64e(0xc3c3b2712f719d2f),
	C64e(0xbebe8638e13867e1), C64e(0x3535c8fda2fd6aa2),
	C64e(0x8888c74fcc4f0bcc), C64e(0x2e2e654b394b5c39),
	C64e(0x93936af957f93d57), C64e(0x5555580df20daaf2),
	C64e(0xfcfc619d829de382), C64e(0x7a7ab3c947c9f447),
	C64e(0xc8c827efacef8bac), C64e(0xbaba8832e7326fe7),
	C64e(0x32324f7d2b7d642b), C64e(0xe6e642a495a4d795),
	C64e(0xc0c03bfba0fb9ba0), C64e(0x1919aab398b33298),
	C64e(0x9e9ef668d16827d1), C64e(0xa3a322817f815d7f),
	C64e(0x4444eeaa66aa8866), C64e(0x5454d6827e82a87e),
	C64e(0x3b3bdde6abe676ab), C64e(0x0b0b959e839e1683),
	C64e(0x8c8cc945ca4503ca), C64e(0xc7c7bc7b297b9529),
	C64e(0x6b6b056ed36ed6d3), C64e(0x28286c443c44503c),
	C64e(0xa7a72c8b798b5579), C64e(0xbcbc813de23d63e2),
	C64e(0x161631271d272c1d), C64e(0xadad379a769a4176),
	C64e(0xdbdb964d3b4dad3b), C64e(0x64649efa56fac856),
	C64e(0x7474a6d24ed2e84e), C64e(0x141436221e22281e),
	C64e(0x9292e476db763fdb), C64e(0x0c0c121e0a1e180a),
	C64e(0x4848fcb46cb4906c), C64e(0xb8b88f37e4376be4),
	C64e(0x9f9f78e75de7255d), C64e(0xbdbd0fb26eb2616e),
	C64e(0x4343692aef2a86ef), C64e(0xc4c435f1a6f193a6),
	C64e(0x3939dae3a8e372a8), C64e(0x3131c6f7a4f762a4),
	C64e(0xd3d38a593759bd37), C64e(0xf2f274868b86ff8b),
	C64e(0xd5d583563256b132), C64e(0x8b8b4ec543c50d43),
	C64e(0x6e6e85eb59ebdc59), C64e(0xdada18c2b7c2afb7),
	C64e(0x01018e8f8c8f028c), C64e(0xb1b11dac64ac7964),
	C64e(0x9c9cf16dd26d23d2), C64e(0x4949723be03b92e0),
	C64e(0xd8d81fc7b4c7abb4), C64e(0xacacb915fa1543fa),
	C64e(0xf3f3fa090709fd07), C64e(0xcfcfa06f256f8525),
	C64e(0xcaca20eaafea8faf), C64e(0xf4f47d898e89f38e),
	C64e(0x47476720e9208ee9), C64e(0x1010382818282018),
	C64e(0x6f6f0b64d564ded5), C64e(0xf0f073838883fb88),
	C64e(0x4a4afbb16fb1946f), C64e(0x5c5cca967296b872),
	C64e(0x3838546c246c7024), C64e(0x57575f08f108aef1),
	C64e(0x73732152c752e6c7), C64e(0x979764f351f33551),
	C64e(0xcbcbae6523658d23), C64e(0xa1a125847c84597c),
	C64e(0xe8e857bf9cbfcb9c), C64e(0x3e3e5d6321637c21),
	C64e(0x9696ea7cdd7c37dd), C64e(0x61611e7fdc7fc2dc),
	C64e(0x0d0d9c9186911a86), C64e(0x0f0f9b9485941e85),
	C64e(0xe0e04bab90abdb90), C64e(0x7c7cbac642c6f842),
	C64e(0x71712657c457e2c4), C64e(0xcccc29e5aae583aa),
	C64e(0x9090e373d8733bd8), C64e(0x0606090f050f0c05),
	C64e(0xf7f7f4030103f501), C64e(0x1c1c2a3612363812),
	C64e(0xc2c23cfea3fe9fa3), C64e(0x6a6a8be15fe1d45f),
	C64e(0xaeaebe10f91047f9), C64e(0x6969026bd06bd2d0),
	C64e(0x1717bfa891a82e91), C64e(0x999971e858e82958),
	C64e(0x3a3a536927697427), C64e(0x2727f7d0b9d04eb9),
	C64e(0xd9d991483848a938), C64e(0xebebde351335cd13),
	C64e(0x2b2be5ceb3ce56b3), C64e(0x2222775533554433),
	C64e(0xd2d204d6bbd6bfbb), C64e(0xa9a9399070904970),
	C64e(0x0707878089800e89), C64e(0x3333c1f2a7f266a7),
	C64e(0x2d2decc1b6c15ab6), C64e(0x3c3c5a6622667822),
	C64e(0x1515b8ad92ad2a92), C64e(0xc9c9a96020608920),
	C64e(0x87875cdb49db1549), C64e(0xaaaab01aff1a4fff),
	C64e(0x5050d8887888a078), C64e(0xa5a52b8e7a8e517a),
	C64e(0x0303898a8f8a068f), C64e(0x59594a13f813b2f8),
	C64e(0x0909929b809b1280), C64e(0x1a1a233917393417),
	C64e(0x65651075da75cada), C64e(0xd7d784533153b531),
	C64e(0x8484d551c65113c6), C64e(0xd0d003d3b8d3bbb8),
	C64e(0x8282dc5ec35e1fc3), C64e(0x2929e2cbb0cb52b0),
	C64e(0x5a5ac3997799b477), C64e(0x1e1e2d3311333c11),
	C64e(0x7b7b3d46cb46f6cb), C64e(0xa8a8b71ffc1f4bfc),
	C64e(0x6d6d0c61d661dad6), C64e(0x2c2c624e3a4e583a)
};

static const sph_u64 T2[] = {
	C64e(0xa5c6c632f4a5f497), C64e(0x84f8f86f978497eb),
	C64e(0x99eeee5eb099b0c7), C64e(0x8df6f67a8c8d8cf7),
	C64e(0x0dffffe8170d17e5), C64e(0xbdd6d60adcbddcb7),
	C64e(0xb1dede16c8b1c8a7), C64e(0x5491916dfc54fc39),
	C64e(0x50606090f050f0c0), C64e(0x0302020705030504),
	C64e(0xa9cece2ee0a9e087), C64e(0x7d5656d1877d87ac),
	C64e(0x19e7e7cc2b192bd5), C64e(0x62b5b513a662a671),
	C64e(0xe64d4d7c31e6319a), C64e(0x9aecec59b59ab5c3),
	C64e(0x458f8f40cf45cf05), C64e(0x9d1f1fa3bc9dbc3e),
	C64e(0x40898949c040c009), C64e(0x87fafa68928792ef),
	C64e(0x15efefd03f153fc5), C64e(0xebb2b29426eb267f),
	C64e(0xc98e8ece40c94007), C64e(0x0bfbfbe61d0b1ded),
	C64e(0xec41416e2fec2f82), C64e(0x67b3b31aa967a97d),
	C64e(0xfd5f5f431cfd1cbe), C64e(0xea45456025ea258a),
	C64e(0xbf2323f9dabfda46), C64e(0xf753535102f702a6),
	C64e(0x96e4e445a196a1d3), C64e(0x5b9b9b76ed5bed2d),
	C64e(0xc27575285dc25dea), C64e(0x1ce1e1c5241c24d9),
	C64e(0xae3d3dd4e9aee97a), C64e(0x6a4c4cf2be6abe98),
	C64e(0x5a6c6c82ee5aeed8), C64e(0x417e7ebdc341c3fc),
	C64e(0x02f5f5f3060206f1), C64e(0x4f838352d14fd11d),
	C64e(0x5c68688ce45ce4d0), C64e(0xf451515607f407a2),
	C64e(0x34d1d18d5c345cb9), C64e(0x08f9f9e1180818e9),
	C64e(0x93e2e24cae93aedf), C64e(0x73abab3e9573954d),
	C64e(0x53626297f553f5c4), C64e(0x3f2a2a6b413f4154),
	C64e(0x0c08081c140c1410), C64e(0x52959563f652f631),
	C64e(0x654646e9af65af8c), C64e(0x5e9d9d7fe25ee221),
	C64e(0x2830304878287860), C64e(0xa13737cff8a1f86e),
	C64e(0x0f0a0a1b110f1114), C64e(0xb52f2febc4b5c45e),
	C64e(0x090e0e151b091b1c), C64e(0x3624247e5a365a48),
	C64e(0x9b1b1badb69bb636), C64e(0x3ddfdf98473d47a5),
	C64e(0x26cdcda76a266a81), C64e(0x694e4ef5bb69bb9c),
	C64e(0xcd7f7f334ccd4cfe), C64e(0x9feaea50ba9fbacf),
	C64e(0x1b12123f2d1b2d24), C64e(0x9e1d1da4b99eb93a),
	C64e(0x745858c49c749cb0), C64e(0x2e343446722e7268),
	C64e(0x2d363641772d776c), C64e(0xb2dcdc11cdb2cda3),
	C64e(0xeeb4b49d29ee2973), C64e(0xfb5b5b4d16fb16b6),
	C64e(0xf6a4a4a501f60153), C64e(0x4d7676a1d74dd7ec),
	C64e(0x61b7b714a361a375), C64e(0xce7d7d3449ce49fa),
	C64e(0x7b5252df8d7b8da4), C64e(0x3edddd9f423e42a1),
	C64e(0x715e5ecd937193bc), C64e(0x971313b1a297a226),
	C64e(0xf5a6a6a204f50457), C64e(0x68b9b901b868b869),
	C64e(0x0000000000000000), C64e(0x2cc1c1b5742c7499),
	C64e(0x604040e0a060a080), C64e(0x1fe3e3c2211f21dd),
	C64e(0xc879793a43c843f2), C64e(0xedb6b69a2ced2c77),
	C64e(0xbed4d40dd9bed9b3), C64e(0x468d8d47ca46ca01),
	C64e(0xd967671770d970ce), C64e(0x4b7272afdd4bdde4),
	C64e(0xde9494ed79de7933), C64e(0xd49898ff67d4672b),
	C64e(0xe8b0b09323e8237b), C64e(0x4a85855bde4ade11),
	C64e(0x6bbbbb06bd6bbd6d), C64e(0x2ac5c5bb7e2a7e91),
	C64e(0xe54f4f7b34e5349e), C64e(0x16ededd73a163ac1),
	C64e(0xc58686d254c55417), C64e(0xd79a9af862d7622f),
	C64e(0x55666699ff55ffcc), C64e(0x941111b6a794a722),
	C64e(0xcf8a8ac04acf4a0f), C64e(0x10e9e9d9301030c9),
	C64e(0x0604040e0a060a08), C64e(0x81fefe66988198e7),
	C64e(0xf0a0a0ab0bf00b5b), C64e(0x447878b4cc44ccf0),
	C64e(0xba2525f0d5bad54a), C64e(0xe34b4b753ee33e96),
	C64e(0xf3a2a2ac0ef30e5f), C64e(0xfe5d5d4419fe19ba),
	C64e(0xc08080db5bc05b1b), C64e(0x8a050580858a850a),
	C64e(0xad3f3fd3ecadec7e), C64e(0xbc2121fedfbcdf42),
	C64e(0x487070a8d848d8e0), C64e(0x04f1f1fd0c040cf9),
	C64e(0xdf6363197adf7ac6), C64e(0xc177772f58c158ee),
	C64e(0x75afaf309f759f45), C64e(0x634242e7a563a584),
	C64e(0x3020207050305040), C64e(0x1ae5e5cb2e1a2ed1),
	C64e(0x0efdfdef120e12e1), C64e(0x6dbfbf08b76db765),
	C64e(0x4c818155d44cd419), C64e(0x141818243c143c30),
	C64e(0x352626795f355f4c), C64e(0x2fc3c3b2712f719d),
	C64e(0xe1bebe8638e13867), C64e(0xa23535c8fda2fd6a),
	C64e(0xcc8888c74fcc4f0b), C64e(0x392e2e654b394b5c),
	C64e(0x5793936af957f93d), C64e(0xf25555580df20daa),
	C64e(0x82fcfc619d829de3), C64e(0x477a7ab3c947c9f4),
	C64e(0xacc8c827efacef8b), C64e(0xe7baba8832e7326f),
	C64e(0x2b32324f7d2b7d64), C64e(0x95e6e642a495a4d7),
	C64e(0xa0c0c03bfba0fb9b), C64e(0x981919aab398b332),
	C64e(0xd19e9ef668d16827), C64e(0x7fa3a322817f815d),
	C64e(0x664444eeaa66aa88), C64e(0x7e5454d6827e82a8),
	C64e(0xab3b3bdde6abe676), C64e(0x830b0b959e839e16),
	C64e(0xca8c8cc945ca4503), C64e(0x29c7c7bc7b297b95),
	C64e(0xd36b6b056ed36ed6), C64e(0x3c28286c443c4450),
	C64e(0x79a7a72c8b798b55), C64e(0xe2bcbc813de23d63),
	C64e(0x1d161631271d272c), C64e(0x76adad379a769a41),
	C64e(0x3bdbdb964d3b4dad), C64e(0x5664649efa56fac8),
	C64e(0x4e7474a6d24ed2e8), C64e(0x1e141436221e2228),
	C64e(0xdb9292e476db763f), C64e(0x0a0c0c121e0a1e18),
	C64e(0x6c4848fcb46cb490), C64e(0xe4b8b88f37e4376b),
	C64e(0x5d9f9f78e75de725), C64e(0x6ebdbd0fb26eb261),
	C64e(0xef4343692aef2a86), C64e(0xa6c4c435f1a6f193),
	C64e(0xa83939dae3a8e372), C64e(0xa43131c6f7a4f762),
	C64e(0x37d3d38a593759bd), C64e(0x8bf2f274868b86ff),
	C64e(0x32d5d583563256b1), C64e(0x438b8b4ec543c50d),
	C64e(0x596e6e85eb59ebdc), C64e(0xb7dada18c2b7c2af),
	C64e(0x8c01018e8f8c8f02), C64e(0x64b1b11dac64ac79),
	C64e(0xd29c9cf16dd26d23), C64e(0xe04949723be03b92),
	C64e(0xb4d8d81fc7b4c7ab), C64e(0xfaacacb915fa1543),
	C64e(0x07f3f3fa090709fd), C64e(0x25cfcfa06f256f85),
	C64e(0xafcaca20eaafea8f), C64e(0x8ef4f47d898e89f3),
	C64e(0xe947476720e9208e), C64e(0x1810103828182820),
	C64e(0xd56f6f0b64d564de), C64e(0x88f0f073838883fb),
	C64e(0x6f4a4afbb16fb194), C64e(0x725c5cca967296b8),
	C64e(0x243838546c246c70), C64e(0xf157575f08f108ae),
	C64e(0xc773732152c752e6), C64e(0x51979764f351f335),
	C64e(0x23cbcbae6523658d), C64e(0x7ca1a125847c8459),
	C64e(0x9ce8e857bf9cbfcb), C64e(0x213e3e5d6321637c),
	C64e(0xdd9696ea7cdd7c37), C64e(0xdc61611e7fdc7fc2),
	C64e(0x860d0d9c9186911a), C64e(0x850f0f9b9485941e),
	C64e(0x90e0e04bab90abdb), C64e(0x427c7cbac642c6f8),
	C64e(0xc471712657c457e2), C64e(0xaacccc29e5aae583),
	C64e(0xd89090e373d8733b), C64e(0x050606090f050f0c),
	C64e(0x01f7f7f4030103f5), C64e(0x121c1c2a36123638),
	C64e(0xa3c2c23cfea3fe9f), C64e(0x5f6a6a8be15fe1d4),
	C64e(0xf9aeaebe10f91047), C64e(0xd06969026bd06bd2),
	C64e(0x911717bfa891a82e), C64e(0x58999971e858e829),
	C64e(0x273a3a5369276974), C64e(0xb92727f7d0b9d04e),
	C64e(0x38d9d991483848a9), C64e(0x13ebebde351335cd),
	C64e(0xb32b2be5ceb3ce56), C64e(0x3322227755335544),
	C64e(0xbbd2d204d6bbd6bf), C64e(0x70a9a93990709049),
	C64e(0x890707878089800e), C64e(0xa73333c1f2a7f266),
	C64e(0xb62d2decc1b6c15a), C64e(0x223c3c5a66226678),
	C64e(0x921515b8ad92ad2a), C64e(0x20c9c9a960206089),
	C64e(0x4987875cdb49db15), C64e(0xffaaaab01aff1a4f),
	C64e(0x785050d8887888a0), C64e(0x7aa5a52b8e7a8e51),
	C64e(0x8f0303898a8f8a06), C64e(0xf859594a13f813b2),
	C64e(0x800909929b809b12), C64e(0x171a1a2339173934),
	C64e(0xda65651075da75ca), C64e(0x31d7d784533153b5),
	C64e(0xc68484d551c65113), C64e(0xb8d0d003d3b8d3bb),
	C64e(0xc38282dc5ec35e1f), C64e(0xb02929e2cbb0cb52),
	C64e(0x775a5ac3997799b4), C64e(0x111e1e2d3311333c),
	C64e(0xcb7b7b3d46cb46f6), C64e(0xfca8a8b71ffc1f4b),
	C64e(0xd66d6d0c61d661da), C64e(0x3a2c2c624e3a4e58)
};

static const sph_u64 T3[] = {
	C64e(0x97a5c6c632f4a5f4), C64e(0xeb84f8f86f978497),
	C64e(0xc799eeee5eb099b0), C64e(0xf78df6f67a8c8d8c),
	C64e(0xe50dffffe8170d17), C64e(0xb7bdd6d60adcbddc),
	C64e(0xa7b1dede16c8b1c8), C64e(0x395491916dfc54fc),
	C64e(0xc050606090f050f0), C64e(0x0403020207050305),
	C64e(0x87a9cece2ee0a9e0), C64e(0xac7d5656d1877d87),
	C64e(0xd519e7e7cc2b192b), C64e(0x7162b5b513a662a6),
	C64e(0x9ae64d4d7c31e631), C64e(0xc39aecec59b59ab5),
	C64e(0x05458f8f40cf45cf), C64e(0x3e9d1f1fa3bc9dbc),
	C64e(0x0940898949c040c0), C64e(0xef87fafa68928792),
	C64e(0xc515efefd03f153f), C64e(0x7febb2b29426eb26),
	C64e(0x07c98e8ece40c940), C64e(0xed0bfbfbe61d0b1d),
	C64e(0x82ec41416e2fec2f), C64e(0x7d67b3b31aa967a9),
	C64e(0xbefd5f5f431cfd1c), C64e(0x8aea45456025ea25),
	C64e(0x46bf2323f9dabfda), C64e(0xa6f753535102f702),
	C64e(0xd396e4e445a196a1), C64e(0x2d5b9b9b76ed5bed),
	C64e(0xeac27575285dc25d), C64e(0xd91ce1e1c5241c24),
	C64e(0x7aae3d3dd4e9aee9), C64e(0x986a4c4cf2be6abe),
	C64e(0xd85a6c6c82ee5aee), C64e(0xfc417e7ebdc341c3),
	C64e(0xf102f5f5f3060206), C64e(0x1d4f838352d14fd1),
	C64e(0xd05c68688ce45ce4), C64e(0xa2f451515607f407),
	C64e(0xb934d1d18d5c345c), C64e(0xe908f9f9e1180818),
	C64e(0xdf93e2e24cae93ae), C64e(0x4d73abab3e957395),
	C64e(0xc453626297f553f5), C64e(0x543f2a2a6b413f41),
	C64e(0x100c08081c140c14), C64e(0x3152959563f652f6),
	C64e(0x8c654646e9af65af), C64e(0x215e9d9d7fe25ee2),
	C64e(0x6028303048782878), C64e(0x6ea13737cff8a1f8),
	C64e(0x140f0a0a1b110f11), C64e(0x5eb52f2febc4b5c4),
	C64e(0x1c090e0e151b091b), C64e(0x483624247e5a365a),
	C64e(0x369b1b1badb69bb6), C64e(0xa53ddfdf98473d47),
	C64e(0x8126cdcda76a266a), C64e(0x9c694e4ef5bb69bb),
	C64e(0xfecd7f7f334ccd4c), C64e(0xcf9feaea50ba9fba),
	C64e(0x241b12123f2d1b2d), C64e(0x3a9e1d1da4b99eb9),
	C64e(0xb0745858c49c749c), C64e(0x682e343446722e72),
	C64e(0x6c2d363641772d77), C64e(0xa3b2dcdc11cdb2cd),
	C64e(0x73eeb4b49d29ee29), C64e(0xb6fb5b5b4d16fb16),
	C64e(0x53f6a4a4a501f601), C64e(0xec4d7676a1d74dd7),
	C64e(0x7561b7b714a361a3), C64e(0xface7d7d3449ce49),
	C64e(0xa47b5252df8d7b8d), C64e(0xa13edddd9f423e42),
	C64e(0xbc715e5ecd937193), C64e(0x26971313b1a297a2),
	C64e(0x57f5a6a6a204f504), C64e(0x6968b9b901b868b8),
	C64e(0x0000000000000000), C64e(0x992cc1c1b5742c74),
	C64e(0x80604040e0a060a0), C64e(0xdd1fe3e3c2211f21),
	C64e(0xf2c879793a43c843), C64e(0x77edb6b69a2ced2c),
	C64e(0xb3bed4d40dd9bed9), C64e(0x01468d8d47ca46ca),
	C64e(0xced967671770d970), C64e(0xe44b7272afdd4bdd),
	C64e(0x33de9494ed79de79), C64e(0x2bd49898ff67d467),
	C64e(0x7be8b0b09323e823), C64e(0x114a85855bde4ade),
	C64e(0x6d6bbbbb06bd6bbd), C64e(0x912ac5c5bb7e2a7e),
	C64e(0x9ee54f4f7b34e534), C64e(0xc116ededd73a163a),
	C64e(0x17c58686d254c554), C64e(0x2fd79a9af862d762),
	C64e(0xcc55666699ff55ff), C64e(0x22941111b6a794a7),
	C64e(0x0fcf8a8ac04acf4a), C64e(0xc910e9e9d9301030),
	C64e(0x080604040e0a060a), C64e(0xe781fefe66988198),
	C64e(0x5bf0a0a0ab0bf00b), C64e(0xf0447878b4cc44cc),
	C64e(0x4aba2525f0d5bad5), C64e(0x96e34b4b753ee33e),
	C64e(0x5ff3a2a2ac0ef30e), C64e(0xbafe5d5d4419fe19),
	C64e(0x1bc08080db5bc05b), C64e(0x0a8a050580858a85),
	C64e(0x7ead3f3fd3ecadec), C64e(0x42bc2121fedfbcdf),
	C64e(0xe0487070a8d848d8), C64e(0xf904f1f1fd0c040c),
	C64e(0xc6df6363197adf7a), C64e(0xeec177772f58c158),
	C64e(0x4575afaf309f759f), C64e(0x84634242e7a563a5),
	C64e(0x4030202070503050), C64e(0xd11ae5e5cb2e1a2e),
	C64e(0xe10efdfdef120e12), C64e(0x656dbfbf08b76db7),
	C64e(0x194c818155d44cd4), C64e(0x30141818243c143c),
	C64e(0x4c352626795f355f), C64e(0x9d2fc3c3b2712f71),
	C64e(0x67e1bebe8638e138), C64e(0x6aa23535c8fda2fd),
	C64e(0x0bcc8888c74fcc4f), C64e(0x5c392e2e654b394b),
	C64e(0x3d5793936af957f9), C64e(0xaaf25555580df20d),
	C64e(0xe382fcfc619d829d), C64e(0xf4477a7ab3c947c9),
	C64e(0x8bacc8c827efacef), C64e(0x6fe7baba8832e732),
	C64e(0x642b32324f7d2b7d), C64e(0xd795e6e642a495a4),
	C64e(0x9ba0c0c03bfba0fb), C64e(0x32981919aab398b3),
	C64e(0x27d19e9ef668d168), C64e(0x5d7fa3a322817f81),
	C64e(0x88664444eeaa66aa), C64e(0xa87e5454d6827e82),
	C64e(0x76ab3b3bdde6abe6), C64e(0x16830b0b959e839e),
	C64e(0x03ca8c8cc945ca45), C64e(0x9529c7c7bc7b297b),
	C64e(0xd6d36b6b056ed36e), C64e(0x503c28286c443c44),
	C64e(0x5579a7a72c8b798b), C64e(0x63e2bcbc813de23d),
	C64e(0x2c1d161631271d27), C64e(0x4176adad379a769a),
	C64e(0xad3bdbdb964d3b4d), C64e(0xc85664649efa56fa),
	C64e(0xe84e7474a6d24ed2), C64e(0x281e141436221e22),
	C64e(0x3fdb9292e476db76), C64e(0x180a0c0c121e0a1e),
	C64e(0x906c4848fcb46cb4), C64e(0x6be4b8b88f37e437),
	C64e(0x255d9f9f78e75de7), C64e(0x616ebdbd0fb26eb2),
	C64e(0x86ef4343692aef2a), C64e(0x93a6c4c435f1a6f1),
	C64e(0x72a83939dae3a8e