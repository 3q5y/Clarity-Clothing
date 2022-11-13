// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"
#include "keystore.h"
#include "main.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet_ismine.h"
#endif

#include <vector>

#include <boost/test/unit_test.hpp>

using namespace std;

// Helpers:
static std::vector<unsigned char>
Serialize(const CScript& s)
{
    std::vector<unsigned char> sSerialized(s);
    return sSerialized;
}

static bool
Verify(const CScript& scriptSig, const CScript& scriptPubKey, bool fStrict, ScriptError& err)
{
    // Create dummy to/from transactions:
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;

    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vin[0].scriptSig = scriptSig;
    txTo.vout[0].nValue = 1;

    return VerifyScript(scriptSig, scriptPubKey, fStrict ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo, 0), &err);
}


BOOST_AUTO_TEST_SUITE(script_P2SH_tests)

BOOST_AUTO_TEST_CASE(sign)
{
    LOCK(cs_main);
    // Pay-to-script-hash looks like this:
    // scriptSig:    <sig> <sig...> <serialized_script>
    // scriptPubKey: HASH160 <hash> EQUAL

    // Test SignSignature() (and therefore the version of Solver() that signs transactions)
    CBasicKeyStore keystore;
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
    }

    // 8 Scripts: checking all combinations of
    // different keys, straight/P2SH, pubkey/pubkeyhash
    CScript standardScripts[4];
    standardScripts[0] << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
    standardScripts[1] = GetScriptForDestination(key[1].GetPubKey().GetID());
    standardScripts[2] << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;
    standardScripts[3] = GetScriptForDestination(key[2].GetPubKey().GetID());
    CScript evalScripts[4];
    for (int i = 0; i < 4; i++)
    {
        keystore.AddCScript(standardScripts[i]);
        evalScripts[i] = GetScriptForDestination(CScriptID(standardScripts[i]));
    }

    CMutableTransaction txFrom;  // Funding transaction:
    string reason;
    txFrom.vout.resize(8);
    for (int i = 0; i < 4; i++)
    {
        txFrom.vout[i].scriptPubKey = evalScripts[i];
        txFrom.vout[i].nValue = COIN;
        txFrom.vout[i+4].scriptPubKey = standardScripts[i];
        txFrom.vout[i+4].nValue = COIN;
    }
    BOOST_CHECK(IsStandardTx(txFrom, reason));

    CMutableTransaction txTo[8]; // Spending transactions
    for (int i = 0; i < 8; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
#ifdef ENABLE_WALLET
        BOOST_CHECK_MESSAGE(IsMine(keystore, txFrom.vout[i].scriptPubKey), strprintf("IsMine %d", i));
#endif
    }
    for (int i = 0; i < 8; i++)
    {
        BOOST_CHECK_MESSAGE(SignSignature(keystore, txFrom, txTo[i], 0), strprintf("SignSignature %d", i));
    }
    // All of the above should be OK, and the txTos have valid signatures
    // Check to make sure signature verification fails if we use the wrong ScriptSig:
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
        {
            CScript sigSave = txTo[i].vin[0].scriptSig;
            txTo[i].vin[0].scriptSig = txTo[j].vin[0].scriptSig;
            bool sigOK = CScriptCheck(CCoins(txFrom, 0), txTo[i], 0, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, false)();
            if (i == j)
                BOOST_CHECK_MESSAGE(sigOK, strprintf("VerifySignature %d %d", i, j));
            else
                BOOST_CHECK_MESSAGE(!sigOK, strprintf("VerifySignature %d %d", i, j));
            txTo[i].vin[0].scriptSig = sigSave;
        }
}

BOOST_AUTO_TEST_CASE(norecurse)
{
    ScriptError err;
    // Make sure only the outer pay-to-script-hash does the
    // extra-validation thing:
    CScript invalidAsScript;
    invalidAsScript << OP_INVALIDOPCODE << OP_INVALIDOPCODE;

    CScript p2sh = GetScriptForDestination(CScriptID(invalidAsScript));

    CScript scriptSig;
    scriptSig << Serialize(invalidAsScript);

    // Should not verify, because it will try to execute OP_INVALIDOPCODE
    BOOST_CHECK(!Verify(scriptSig, p2sh, true, err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_BAD_OPCODE, ScriptErrorString(err));

    // Try to recur, and verification should succeed because
    // the inner HASH160 <> EQUAL should only check the hash:
    CScript p2sh2 = GetScriptForDestination(CScriptID(p2sh));
    CScript scriptSig2;
    scriptSig2 << Serialize(invalidAsScript) << Serialize(p2sh);

    BOOST_CHECK(Verify(scriptSig2, p