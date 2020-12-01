// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BIGNUM_H
#define BITCOIN_BIGNUM_H

#if defined HAVE_CONFIG_H
#include "config/giant-config.h"
#endif

#if defined(USE_NUM_OPENSSL)
#include <openssl/bn.h>
#endif
#if defined(USE_NUM_GMP)
#include <gmp.h>
#endif

#include <stdexcept>
#include <vector>
#include <limits.h>

#include "serialize.h"
#include "uint256.h"
#include "version.h"
#include "