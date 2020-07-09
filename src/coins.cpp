// Copyright (c) 2012-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "random.h"

#include <assert.h>

/**
 * calculate number of bytes for the bitmask, and its number of non-zero bytes
 * each bit in the bitmask represents the availability of one output, but the
 * availabilities of the first two ou