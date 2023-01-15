#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.blocktools import create_block, create_coinbase

'''
SendHeadersTest -- test behavior of headers messages to announce blocks.

Setup:

- Two nodes, two p2p connections to node0. One p2p connection should only ever
  receive inv's (omitted from testing description below, this is our control).
  Second node is used for creating reorgs.

Part 1: No headers announcements before "sendheaders"
a. node mines a block [expect: inv]
   send getdata for the block [expect: block]
b. node mines another block [expect: inv]
   send g