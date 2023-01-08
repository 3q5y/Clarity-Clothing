#!/usr/bin/env python3
# Copyright (c) 2014-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test node disconnect and ban behavior"""
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes_bi,
    wait_until,
)

class DisconnectBanTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2

    def run_test(self):
        self.log.info("Test setban and listbanned RPCs")

        self.log.info("setban: successfully ban single IP address")
        assert_equal(len(self.nodes[1].getpeerinfo()), 2)  # node1 should have 2 connections to node0 at this point
        self.nodes[1].setban("127.0.0.1", "add")
        wait_until(lambda: len(self.nodes[1].getpeerinfo()) == 0, timeout=10)
        assert_equal(len(self.nodes[1].getpeerinfo()), 0)  # all nodes must be disconnected at this point
        assert_equal(len(self.nodes[1].listbanned()), 1)

        self.log.info("clearbanned: successfully clear ban list")
        self.nodes[1].clearbanned()
        assert_equal(len(self.nodes[1].listbanned()), 0)
        self.nodes[1].setban("127.0.0.0/24", "add")

        self.log.info("setban: fail to ban an already banned subnet")
        assert_equal(len(self.nodes[1].listbanned()), 1)
        assert_raises_rpc_error(-23, "IP/Subnet already banned", self.nodes[1].setban, "127.0.0.1", "add")

        self.log.info("setban: fail to ban an invalid subnet")
        assert_raises_rpc_error(-23, "Error: Invalid IP/Subnet", self.nodes[1].setban, "127.0.0.1/42", "add")
        assert_equal(len(self.nodes[1].listbanned()), 1)  # still only one banned ip because 127.0.0.1 is within the range of 127.0.0.0/24

        self.log.info("setban remove: fail to unban a non-banned subnet")
        assert_raises_rpc_error(-1, "Error: Unban failed", self.nodes[1].setban, "127.0.0.1", "remove")
        assert_equal(len(self.nodes[1].listbanned()), 1)

        self.log.info("setban remove: successfully unban subnet")
        self.nodes[1].setban("127.0.0.0/24", "remove")
        assert_equal(len(self.nodes[1].listbanned()), 0)
        self.nodes[1].clearbanned()
        assert_equal(len(self.nodes[1].listbanned()), 0)

        self.log.info("setban: test persistence across node restart")
        self.nodes[1].setban("127.0.0.0/32", "add")
        self.nodes[1].setban("127.0.0.0/24", "add")
        # Set the mocktime so we can control when bans expire
        old_time = int(time.time())
        self.nodes[1].setmocktime(old_time)
        self.nodes[1].setban("192.168.0.1", "add", 1)  # ban for 1 seconds
        self.nodes[1].setban("2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/19", "add", 1000)  # ban for 1000 seconds
        listBeforeShutdown = self.nodes[1].listbanned()
        assert_equal("192.168.0.1/32", listBeforeShutdown[2]['address'])
        # Move time forward by 3 seconds so the third ban has expired
        self.nodes[1].setmocktime(old_time + 3)
        assert_equal(len(self.nodes[1].listbanned()), 4)

        self.stop_node(1)
        self.start_node(1)

        listAfterShutdown = self.nodes[1].listbanned()
        as