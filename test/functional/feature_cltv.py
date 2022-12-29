#!/usr/bin/env python3
# Copyright (c) 2015-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test BIP65 (CHECKLOCKTIMEVERIFY).

Test that the CHECKLOCKTIMEVERIFY soft-fork activates at (regtest) block height
1351.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import *
from test_framework.blocktools import create_coinbase, create_block
from test_framework.script import CScript, OP_1NEGATE, OP_CHECKLOCKTIMEVERIFY, OP_DROP, CScriptNum
from io import BytesIO

CLTV_HEIGHT = 750

# Reject codes that we might receive in this test
REJECT_INVALID = 16
REJECT_OBSOLETE = 17
REJECT_NONSTANDARD = 64

def cltv_invalidate(tx):
    '''Modify the signature in vin 0 of the tx to fail CLTV

    Prepends -1 CLTV DROP in the scriptSig itself.

    TODO: test more ways that transactions using CLTV could be invalid (eg
    locktime requirements fail, sequence time requirements fail, etc).
    '''
    tx.vin[0].scriptSig = CScript([OP_1NEGATE, OP_CHECKLOCKTIMEVERIFY, OP_DROP] +
                                  list(CScript(tx.vin[0].scriptSig)))

def cltv_validate(node, tx, height):
    '''Modify the signature in vin 0 of the tx to pass CLTV
    Prepends <height> CLTV DROP in the scriptSig, and sets
    the locktime to height'''
    tx.vin[0].nSequence = 0
    tx.nLockTime = height

    # Need to re-sign, since nSequence and nLockTime changed
    signed_result = node.signrawtransaction(ToHex(tx))
    new_tx = CTransaction()
    new_tx.deserialize(BytesIO(hex_str_to_bytes(signed_result['hex'])))

    new_tx.vin[0].scriptSig = CScript([CScriptNum(height), OP_CHECKLOCKTIMEVERIFY, OP_DROP] +
                                  list(CScript(new_tx.vin[0].scriptSig)))
    return new_tx

def create_transaction(node, coinbase, to_address, amount):
    from_txid = node.getblock(coinbase)['tx'][0]
    inputs = [{ "txid" : from_txid, "vout" : 0}]
    outputs = { to_address : amount }
    rawtx = node.createrawtransaction(inputs, outputs)
    signresult = node.signrawtransaction(rawtx)
    tx = CTransaction()
    tx.deserialize(BytesIO(hex_str_to_bytes(signresult['hex'])))
    return tx

class BIP65Test(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [['-promiscuousmempoolflags=1', '-