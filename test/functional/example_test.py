#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""An example functional test

The module-level docstring should include a high-level description of
what the test is doing. It's the first thing people see when they open
the file and should give the reader information about *what* the test
is testing and *how* it's being tested
"""
# Imports should be in PEP8 ordering (std library first, then third party
# libraries then local imports).
from collections import defaultdict

# Avoid wildcard * imports if possible
from test_framework.blocktools import (create_block, create_coinbase)
from test_framework.mininode import (
    CInv,
    P2PInterface,
    mininode_lock,
    msg_block,
    msg_getdata,
    network_thread_join,
    network_thread_start,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes,
    wait_until,
)

# P2PInterface is a class containing callbacks to be executed when a P2P
# message is received from the node-under-test. Subclass P2PInterface and
# override the on_*() methods if you need custom behaviour.
class BaseNode(P2PInterface):
    def __init__(self):
        """Initialize the P2PInterface

        Used to inialize custom properties for the Node that aren't
        included by default in the base class. Be aware that the P2PInterface
        base class already stores a counter for each P2P message type and the
        last received message of each type, which should be sufficient for the
        needs of most tests.

        Call super().__init__() first for standard initialization and then
        initialize custom properties."""
        super().__init__()
        # Stores a dictionary of all blocks received
        self.block_receive_map = defaultdict(int)

    def on_block(self, message):
        """Override the standard on_block callback

        Store the hash of a received block in the dictionary."""
        message.block.calc_sha256()
        self.block_receive_map[message.block.sha256] += 1

    def on_inv(self, message):
        """Override the standard on_inv callback"""
        pass

def custom_function():
    """Do some custom behaviour

    If this function is more generally useful for other tests, consider
    moving it to a module in test_framework."""
    # self.log.info("running custom_function")  # Oops! Can't run self.log outside the BitcoinTestFramework
    pass

class ExampleTest(BitcoinTestFramework):
    # Each functional test is a subclass of the BitcoinTestFramework class.

    # Override the set_test_params(), add_options(), setup_chain(), setup_network()
    # and setup_nodes() methods to customize the test setup as required.

    def set_test_params(self):
        """Override test parameters for your individual test.

        This method must be overridden and num_nodes must be exlicitly set."""
        self.setup_clean_chain = True
        self.num_nodes = 3
        # Use self.extra_args to change command-line arguments for the nodes
        self.extra_args = [[], ["-logips"], []]

        # self.log.info("I've finished set_test_params")  # Oops! Can't run self.log before run_test()

    # Use add_options() to add specific command-line options for your test.
    # In practice this is n