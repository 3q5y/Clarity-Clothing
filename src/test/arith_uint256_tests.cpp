// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <stdint.h>
#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include "uint256.h"
#include "arith_uint256.h"
#include <string>
#include "version.h"

BOOST_AUTO_TEST_SUITE(arith_uint256_tests)
///BOOST_FIXTURE_TEST_SUITE(arith_uint256_tests, BasicTestingSetup)

/// Convert vector to arith_uint256, via uint256 blob
inline arith_uint256 arith_uint256V(const std::vector<unsigned char>& vch)
{
    return UintToArith256(uint256(vch));
}

const unsigned char R1Array[] =
    "\x9c\x52\x4a\xdb\xcf\x56\x11\x12\x2b\x29\