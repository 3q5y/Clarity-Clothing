// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "core_io.h"
#include "init.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "primitives/transaction.h"
#include "zgic/deterministicmint.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "swifttx.h"
#include "uint256.h"
#include "utilmoneystr.h"
#include "zgicchain.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

using namespace boost;
using namespace boost::assign;
using namespace std;

void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out.push_back(Pair("asm", scriptPubKey.ToString()));
    if (fIncludeHex)
        out.push_back(Pair("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end())));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        out.push_back(Pair("type", GetTxnOutputType(type)));
        return;
    }

    out.push_back(Pair("reqSigs", nRequired));
    out.push_back(Pair("type", GetTxnOutputType(type)));

    UniValue a(UniValue::VARR);
    for (const CTxDestination& addr : addresses)
        a.push_back(CBitcoinAddress(addr).ToString());
    out.push_back(Pair("addresses", a));
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry)
{
    // Call into TxToUniv() in bitcoin-common to decode the transaction hex.
    //
    // Blockchain contextual information (confirmations and blocktime) is not
    // available to code in bitcoin-common, so we query them here and push the
    // data into the returned UniValue.
    TxToUniv(tx, uint256(), entry);

    if (!hashBlock.IsNull()) {
        entry.push_back(Pair("blockhash", hashBlock.GetHex()));
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                entry.push_back(Pair("confirmations", 1 + chainActive.Height() - pindex->nHeight));
                entry.push_back(Pair("time", pindex->GetBlockTime()));
                entry.push_back(Pair("blocktime", pindex->GetBlockTime()));
            }
            else
                entry.push_back(Pair("confirmations", 0));
        }
    }
}

UniValue getrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "getrawtransaction \"txid\" ( verbose \"blockhash\" )\n"

            "\nNOTE: By default this function only works sometimes. This is when the tx is in the mempool\n"
            "or there is an unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option.\n"

            "\nReturn the raw transaction data.\n"
            "\nIf verbose is 'true', returns an Object with information about 'txid'.\n"
            "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose     (bool, optional, default=false) If false, return a string, otherwise return a json object\n"
            "3. \"blockhash\" (string, optional) The block in which to look for the transaction\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"in_active_chain\": b, (bool) Whether specified block is in the active chain or not (only present with explicit \"blockhash\" argument)\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"size\" : n,             (numeric) The serialized transaction size\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in btc\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"giantaddress\"        (string) giant address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", true")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" false \"myblockhash\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true \"myblockhash\"")
        );

    LOCK(cs_main);

    bool in_active_chain = true;
    uint256 hash = ParseHashV(params[0], "parameter 1");
    CBlockIndex* blockindex = nullptr;

    bool fVerbose = false;
    if (!params[1].isNull()) {
        fVerbose = params[1].isNum() ? (params[1].get_int() != 0) : params[1].get_bool();
    }

    if (!params[2].isNull()) {
        uint256 blockhash = ParseHashV(params[2], "parameter 3");
        BlockMap::iterator it = mapBlockIndex.find(blockhash);
        if (it == mapBlockIndex.end()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
        }
        blockindex = it->second;
        in_active_chain = chainActive.Contains(blockindex);
    }

    CTransaction tx;
    uint256 hash_block;
    if (!GetTransaction(hash, tx, hash_block, true, blockindex)) {
        std::string errmsg;
        if (blockindex) {
            if (!(blockindex->nStatus & BLOCK_HAVE_DATA)) {
                throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
            }
            errmsg = "No such transaction found in the provided block";
        } else {
            errmsg = fTxIndex
              ? "No such mempool or blockchain transaction"
              : "No such mempool transaction. Use -txindex to enable blockchain transaction queries";
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg + ". Use gettransaction for wallet transactions.");
    }

    if (!fVerbose) {
        return EncodeHexTx(tx);
    }

    UniValue result(UniValue::VOBJ);
    if (blockindex) result.push_back(Pair("in_active_chain", in_active_chain));
    TxToJSON(tx, hash_block, result);
    return result;
}

#ifdef ENABLE_WALLET
UniValue listunspent(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 4)
        throw runtime_error(
            "listunspent (