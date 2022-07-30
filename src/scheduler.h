// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCHEDULER_H
#define BITCOIN_SCHEDULER_H

//
// NOTE:
// boost::thread / boost::function / boost::chrono should be ported to
// std::thread / std::function / std::chrono when we support C++11.
//
#include <boost/function.hpp>
#include <boost/chrono/chron