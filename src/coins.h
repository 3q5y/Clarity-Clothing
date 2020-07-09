// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINS_H
#define BITCOIN_COINS_H

#include "compressor.h"
#include "script/standard.h"
#include "serialize.h"
#include "uint256.h"
#include "undo.h"

#include <assert.h>
#include <stdint.h>

#include <boost/unordered_map.hpp>

/** 

    ****Note - for GIANT we added fCoinStake to the 2nd bit. Keep in mind when reading the following and adjust as needed.
 * Pruned version of CTransaction: only retains metadata and unspent transaction outputs
 *
 * Ser