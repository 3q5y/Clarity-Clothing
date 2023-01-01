#!/usr/bin/env python3
# Copyright (c) 2014-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test fee estimation code."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.script import CScript, OP_1, OP_DROP, OP_2, OP_HASH160, OP_EQUAL, hash160, OP_TRUE
from test_framework.mininode import CTransaction, CTxIn, CTxOut, COutPoint, ToHex, COIN

# Construct 2 trivial P2SH's and the ScriptSigs that spend them
# So we can create many transactions without needing to spend
# time signing.
redeem_script_1 = CScript([OP_1, OP_DROP])
redeem_script_2 = CScript([OP_2, OP_DROP])
P2SH_1 = CScript([OP_HASH160, hash160(redeem_script_1), OP_EQUAL])
P2SH_2 = CScript([OP_HASH160, hash160(redeem_script_2), OP_EQUAL])

# Associated ScriptSig's to spend satisfy P2SH_1 and P2SH_2
SCRIPT_SIG = [CScript([OP_TRUE, redeem_script_1]), CScript([OP_TRUE, redeem_script_2])]

global log

def small_txpuzzle_randfee(from_node, conflist, unconflist, amount, min_fee, fee_increment):
    """
    Create and send a transaction with a random fee.
    The transaction pays to a trivial P2SH script, and assumes that its inputs
    are of the same form.
    The function takes a list of confirmed outputs and unconfirmed outputs
    and attempts to use the confirmed list first for its inputs.
    It adds the newly created outputs to the unconfirmed list.
    Returns (raw transaction, fee)
    """
    # It's best to exponentially distribute our random fees
    # because the buckets are exponentially spaced.
    # Exponentially distributed from 1-128 * fee_increment
    rand_fee = float(fee_increment)*(1.1892**random.randint(0,28))
    # Total fee ranges from min_fee to min_fee + 127*fee_increment
    fee = min_fee - fee_increment + satoshi_round(rand_fee)
    tx = CTransaction()
    total_in = Decimal("0.00000000")
    while total_in <= (amount + fee) and len(conflist) > 0:
        t = conflist.pop(0)
        total_in += t["amount"]
        tx.vin.append(CTxIn(COutPoint(int(t["txid"], 16), t["vout"]), b""))
    if total_in <= amount + fee:
        while total_in <= (amount + fee) and len(unconflist) > 0:
            t = unconflist.pop(0)
            total_in += t["amount"]
            tx.vin.append(CTxIn(COutPoint(int(t["txid"], 16), t["vout"]), b""))
        if total_in <= amount + fee:
            raise RuntimeError("Insufficient funds: need %d, have %d"%(amount+fee, total_in))
    tx.vout.append(CTxOut(int((total_in - amount - fee)*COIN), P2SH_1))
    tx.vout.append(CTxOut(int(amount*COIN), P2SH_2))
    # These transactions don't need to be signed, but we still have to insert
    # the ScriptSig that will satisfy the ScriptPubKey.
    for inp in tx.vin:
        inp.scriptSig = SCRIPT_SIG[inp.prevout.n]
    txid = from_node.sendrawtransaction(ToHex(tx), True)
    unconflist.append({ "txid" : txid, "vout" : 0 , "amount" : total_in - amount - fee})
    unconflist.append({ "txid" : txid, "vout" : 1 , "amount" : amount})

    return (ToHex(tx), fee)

def split_inputs(from_node, txins, txouts, initial_split = False):
    """
    We need to generate a lot of inputs so we can generate a ton of transactions.
    This function takes an input from txins, and creates and sends a transaction
    which splits the value into 2 outputs which are appended to txouts.
    Previously this was designed to be small inputs so they wouldn't have
    a high coin age when the notion of priority still existed.
    """
    prevtxout = txins.pop()
    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(int(prevtxout["txid"], 16), prevtxout["vout"]), b""))

    half_change = satoshi_round(prevtxout["amount"]/2)
    rem_change = prevtxout["amount"] - half_change  - Decimal("0.00001000")
    tx.vout.append(CTxOut(int(half_change*COIN), P2SH_1))
    tx.vout.append(CTxOut(int(