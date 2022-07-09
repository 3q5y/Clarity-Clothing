// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "chainparams.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "rpc/server.h"
#include "utilmoneystr.h"

#include <univalue.h>

#include <fstream>
using namespace std;

void budgetToJSON(CBudgetProposal* pbudgetProposal, UniValue& bObj)
{
    CTxDestination address1;
    ExtractDestination(pbudgetProposal->GetPayee(), address1);
    CBitcoinAddress address2(address1);

    bObj.push_back(Pair("Name", pbudgetProposal->GetName()));
    bObj.push_back(Pair("URL", pbudgetProposal->GetURL()));
    bObj.push_back(Pair("Hash", pbudgetProposal->GetHash().ToString()));
    bObj.push_back(Pair("FeeHash", pbudgetProposal->nFeeTXHash.ToString()));
    bObj.push_back(Pair("BlockStart", (int64_t)pbudgetProposal->GetBlockStart()));
    bObj.push_back(Pair("BlockEnd", (int64_t)pbudgetProposal->GetBlockEnd()));
    bObj.push_back(Pair("TotalPaymentCount", (int64_t)pbudgetProposal->GetTotalPaymentCount()));
    bObj.push_back(Pair("RemainingPaymentCount", (int64_t)pbudgetProposal->GetRemainingPaymentCount()));
    bObj.push_back(Pair("PaymentAddress", address2.ToString()));
    bObj.push_back(Pair("Ratio", pbudgetProposal->GetRatio()));
    bObj.push_back(Pair("Yeas", (int64_t)pbudgetProposal->GetYeas()));
    bObj.push_back(Pair("Nays", (int64_t)pbudgetProposal->GetNays()));
    bObj.push_back(Pair("Abstains", (int64_t)pbudgetProposal->GetAbstains()));
    bObj.push_back(Pair("TotalPayment", ValueFromAmount(pbudgetProposal->GetAmount() * pbudgetProposal->GetTotalPaymentCount())));
    bObj.push_back(Pair("MonthlyPayment", ValueFromAmount(pbudgetProposal->GetAmount())));
    bObj.push_back(Pair("IsEstablished", pbudgetProposal->IsEstablished()));

    std::string strError = "";
    bObj.push_back(Pair("IsValid", pbudgetProposal->IsValid(strError)));
    bObj.push_back(Pair("IsValidReason", strError.c_str()));
    bObj.push_back(Pair("fValid", pbudgetProposal->fValid));
}

UniValue preparebudget(const UniValue& params, bool fHelp)
{
    int nBlockMin = 0;
    CBlockIndex* pindexPrev = chainActive.Tip();

    if (fHelp || params.size() != 6)
        throw runtime_error(
            "preparebudget \"proposal-name\" \"url\" payment-count block-start \"giant-address\" monthy-payment\n"
            "\nPrepare proposal for network by signing and creating tx\n"

            "\nArguments:\n"
            "1. \"proposal-name\":  (string, required) Desired proposal name (20 character limit)\n"
            "2. \"url\":            (string, required) URL of proposal details (64 character limit)\n"
            "3. payment-count:    (numeric, required) Total number of monthly payments\n"
            "4. block-start:      (numeric, required) Starting super block height\n"
            "5. \"giant-address\":   (string, required) GIANT address to send payments to\n"
            "6. monthly-payment:  (numeric, required) Monthly payment amount\n"

            "\nResult:\n"
            "\"xxxx\"       (string) proposal fee hash (if successful) or error message (if failed)\n"

            "\nExamples:\n" +
            HelpExampleCli("preparebudget", "\"test-proposal\" \"https://forum.giantpay.network/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500") +
            HelpExampleRpc("preparebudget", "\"test-proposal\" \"https://forum.giantpay.network/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500"));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strProposalName = SanitizeString(params[0].get_str());
    if (strProposalName.size() > 20)
        throw runtime_error("Invalid proposal name, limit of 20 characters.");

    std::string strURL = SanitizeString(params[1].get_str());
    if (strURL.size() > 64)
        throw runtime_error("Invalid url, limit of 64 characters.");

    int nPaymentCount = params[2].get_int();
    if (nPaymentCount < 1)
        throw runtime_error("Invalid payment count, must be more than zero.");

    // Start must be in the next budget cycle
    if (pindexPrev != NULL) nBlockMin = pindexPrev->nHeight - pindexPrev->nHeight % Params().GetBudgetCycleBlocks() + Params().GetBudgetCycleBlocks();

    int nBlockStart = params[3].get_int();
    if (nBlockStart % Params().GetBudgetCycleBlocks() != 0) {
        int nNext = pindexPrev->nHeight - pindexPrev->nHeight % Params().GetBudgetCycleBlocks() + Params().GetBudgetCycleBlocks();
        throw runtime_error(strprintf("Invalid block start - must be a budget cycle block. Next valid block: %d", nNext));
    }

    int nBlockEnd = nBlockStart + Params().GetBudgetCycleBlocks() * nPaymentCount; // End must be AFTER current cycle

    if (nBlockStart < nBlockMin)
        throw runtime_error("Invalid block start, must be more than current height.");

    if (nBlockEnd < pindexPrev->nHeight)
        throw runtime_error("Invalid ending block, starting block + (payment_cycle*payments) must be more than current height.");

    CBitcoinAddress address(params[4].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid GIANT address");

    // Parse GIANT address
    CScript scriptPubKey = GetScriptForDestination(address.Get());
    CAmount nAmount = AmountFromValue(params[5]);

    //*************************************************************************

    // create transaction 15 minutes into the future, to allow for confirmation time
    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, 0);

    std::string strError = "";
    if (!budgetProposalBroadcast.IsValid(strError, false))
        throw runtime_error("Proposal is not valid - " + budgetProposalBroadcast.GetHash().ToString() + " - " + strError);

    bool useIX = false; //true;
    // if (params.size() > 7) {
    //     if(params[7].get_str() != "false" && params[7].get_str() != "true")
    //         return "Invalid use_ix, must be true or false";
    //     useIX = params[7].get_str() == "true" ? true : false;
    // }

    CWalletTx wtx;
    if (!pwalletMain->GetBudgetSystemCollateralTX(wtx, budgetProposalBroadcast.GetHash(), useIX)) { // 50 GIC collateral for proposal
        throw runtime_error("Error making collateral transaction for proposal. Please check your wallet balance.");
    }

    // make our change address
    CReserveKey reservekey(pwalletMain);
    //send the tx to the network
    pwalletMain->CommitTransaction(wtx, reservekey, useIX ? "ix" : "tx");

    return wtx.GetHash().ToString();
}

UniValue submitbudget(const UniValue& params, bool fHelp)
{
    int nBlockMin = 0;
    CBlockIndex* pindexPrev = chainActive.Tip();

    if (fHelp || params.size() != 7)
        throw runtime_error(
            "submitbudget \"proposal-name\" \"url\" payment-count block-start \"giant-address\" monthy-payment \"fee-tx\"\n"
            "\nSubmit proposal to the network\n"

            "\nArguments:\n"
            "1. \"proposal-name\":  (string, required) Desired proposal name (20 character limit)\n"
            "2. \"url\":            (string, required) URL of proposal details (64 character limit)\n"
            "3. payment-count:    (numeric, required) Total number of monthly payments\n"
            "4. block-start:      (numeric, required) Starting super block height\n"
            "5. \"giant-address\":   (string, required) GIANT address to send payments to\n"
            "6. monthly-payment:  (numeric, required) Monthly payment amount\n"
            "7. \"fee-tx\":         (string, required) Transaction hash from preparebudget command\n"

            "\nResult:\n"
            "\"xxxx\"       (string) proposal hash (if successful) or error message (if failed)\n"

            "\nExamples:\n" +
            HelpExampleCli("submitbudget", "\"test-proposal\" \"https://forum.giantpay.network/t/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500") +
            HelpExampleRpc("submitbudget", "\"test-proposal\" \"https://forum.giantpay.network/t/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500"));

    // Check these inputs the same way we check the vote commands:
    // **********************************************************

    std::string strProposalName = SanitizeString(params[0].get_str());
    if (strProposalName.size() > 20)
        throw runtime_error("Invalid proposal name, limit of 20 characters.");

    std::string strURL = SanitizeString(params[1].get_str());
    if (strURL.size() > 64)
        throw runtime_error("Invalid url, limit of 64 characters.");

    int nPaymentCount = params[2].get_int();
    if (nPaymentCount < 1)
        throw runtime_error("Invalid payment count, must be more than zero.");

    // Start must be in the next budget cycle
    if (pindexPrev != NULL) nBlockMin = pindexPrev->nHeight - pindexPrev->nHeight % Params().GetBudgetCycleBlocks() + Params().GetBudgetCycleBlocks();

    int nBlockStart = params[3].get_int();
    if (nBlockStart % Params().GetBudgetCycleBlocks() != 0) {
        int nNext = pindexPrev->nHeight - pindexPrev->nHeight % Params().GetBudgetCycleBlocks() + Params().GetBudgetCycleBlocks();
        throw runtime_error(strprintf("Invalid block start - must be a budget cycle block. Next valid block: %d", nNext));
    }

    int nBlockEnd = nBlockStart + (Params().GetBudgetCycleBlocks() * nPaymentCount); // End must be AFTER current cycle

    if (nBlockStart < nBlockMin)
        throw runtime_error("Invalid block start, must be more than current height.");

    if (nBlockEnd < pindexPrev->nHeight)
        throw runtime_error("Invalid ending block, starting block + (payment_cycle*payments) must be more than current height.");

    CBitcoinAddress address(params[4].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid GIANT address");

    // Parse GIANT address
    CScript scriptPubKey = GetScriptForDestination(address.Get());
    CAmount nAmount = AmountFromValue(params[5]);
    uint256 hash = ParseHashV(params[6], "parameter 1");

    //create the proposal incase we're the first to make it
    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, hash);

    std::string strError = "";
    int nConf = 0;
    if (!IsBudgetCollateralValid(hash, budgetProposalBroadcast.GetHash(), strError, budgetProposalBroadcast.nTime, nConf)) {
        throw runtime_error("Proposal FeeTX is not valid - " + hash.ToString() + " - " + strError);
    }

    if (!masternodeSync.IsBlockchai