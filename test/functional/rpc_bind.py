#!/usr/bin/env python3
# Copyright (c) 2014-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test running bitcoind with the -rpcbind and -rpcallowip options."""

import socket
import sys

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import *
from test_framework.netutil import *

class RPCBindTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.add_nodes(self.num_nodes, None)

    def run_bind_test(self, allow_ips, connect_to, addresses, expected):
        '''
        Start a node with requested rpcallowip and rpcbind parameters,
        then try to connect, and check if the set of bound addresses
        matches the expected set.
        '''
        self.log.info("Bind test for %s" % str(addresses))
        expected = [(addr_to_hex(addr), port) for (addr, port) in expected]
        base_args = ['-disablewallet', '-nolisten']
        if allow_ips:
            base_args += ['-rpcallowip=' + x for x in allow_ips]
        binds = ['-rpcbind='+addr for addr in addresses]
        self.nodes[0].rpchost = connect_to
        self.start_node(0, base_args + binds)
        pid = self.nodes[0].process.pid
        assert_equal(set(get_bind_addrs(pid)), set(expected))
        self.stop_nodes()

    def run_allowip_test(self, allow_ips, rpchost, rpcport):
        '''
        Start a node with rpcallow IP, and request getnetworkinfo
        at a non-localhost IP.
        '''
        self.log.info("Allow IP test for %s:%d" % (rpchost, rpcport))
        base_args = ['-disablewallet', '-nolisten'] + ['-rpcallowip='+x for x in allow_ips]
        self.nodes[0].rpchost = None
        self.start_nodes([base_args])
        # connect to node through non-loopback interface
        node = get_rpc_proxy(rpc_url(get_datadir_path(self.options.tmpdir, 0), 0, "%s:%d" % (rpchost, rpcport)), 0, coveragedir=self.options.coveragedir)
        node.getnetworkinfo()
        self.stop_nodes()

    def run_test(self):
        # due to OS-specific network stats queries, this test works only on Linux
        if not sys.platform.startswith('linux'):
            raise SkipTest("This test can only be run on linux.")
        # find the first non-loopback interface for testing
        non_loopback_ip = None
        for name,ip in all_interfaces(