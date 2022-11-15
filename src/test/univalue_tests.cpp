// Copyright 2014 BitPay, Inc.
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <vector>
#include <string>
#include <map>
#include <univalue.h>

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_AUTO_TEST_SUITE(univalue_tests)

BOOST_AUTO_TEST_CASE(univalue_constructor)
{
    UniValue v1;
    BOOST_CHECK(v1