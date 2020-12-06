
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "main.h"

#include "addrman.h"
#include "chainparams.h"
#include "masternode-budget.h"
#include "masternode-sync.h"
#include "masternode.h"
#include "masternodeman.h"
#include "obfuscation.h"
#include "util.h"
#include <boost/filesystem.hpp>

CBudgetManager budget;
CCriticalSection cs_budget;

std::map<uint256, int64_t> askedForSourceProposalOrBudget;
std::vector<CBudgetProposalBroadcast> vecImmatureBudgetProposals;
std::vector<CFinalizedBudgetBroadcast> vecImmatureFinalizedBudgets;

int nSubmittedFinalBudget;

bool IsBudgetCollateralValid(uint256 nTxCollateralHash, uint256 nExpectedHash, std::string& strError, int64_t& nTime, int& nConf, bool fBudgetFinalization)
{
    CTransaction txCollateral;
    uint256 nBlockHash;
    if (!GetTransaction(nTxCollateralHash, txCollateral, nBlockHash, true)) {
        strError = strprintf("Can't find collateral tx %s", txCollateral.ToString());
        LogPrint("mnbudget","CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }

    if (txCollateral.vout.size() < 1) return false;
    if (txCollateral.nLockTime != 0) return false;

    CScript findScript;
    findScript << OP_RETURN << ToByteVector(nExpectedHash);

    bool foundOpReturn = false;
    for (const CTxOut &o : txCollateral.vout) {
        if (!o.scriptPubKey.IsNormalPaymentScript() && !o.scriptPubKey.IsUnspendable()) {
            strError = strprintf("Invalid Script %s", txCollateral.ToString());
            LogPrint("mnbudget","CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
            return false;
        }
        if (fBudgetFinalization) {
            // Collateral for budget finalization
            // Note: there are still old valid budgets out there, but the check for the new 5 GIC finalization collateral
            //       will also cover the old 50 GIC finalization collateral.
            LogPrint("mnbudget", "Final Budget: o.scriptPubKey(%s) == findScript(%s) ?\n", o.scriptPubKey.ToString(), findScript.ToString());
            if (o.scriptPubKey == findScript) {
                LogPrint("mnbudget", "Final Budget: o.nValue(%ld) >= BUDGET_FEE_TX(%ld) ?\n", o.nValue, BUDGET_FEE_TX);
                if(o.nValue >= BUDGET_FEE_TX) {
                    foundOpReturn = true;
                }
            }
        }
        else {
            // Collateral for normal budget proposal
            LogPrint("mnbudget", "Normal Budget: o.scriptPubKey(%s) == findScript(%s) ?\n", o.scriptPubKey.ToString(), findScript.ToString());
            if (o.scriptPubKey == findScript) {
                LogPrint("mnbudget", "Normal Budget: o.nValue(%ld) >= PROPOSAL_FEE_TX(%ld) ?\n", o.nValue, PROPOSAL_FEE_TX);
                if(o.nValue >= PROPOSAL_FEE_TX) {
                    foundOpReturn = true;
                }
            }
        }
    }
    if (!foundOpReturn) {
        strError = strprintf("Couldn't find opReturn %s in %s", nExpectedHash.ToString(), txCollateral.ToString());
        LogPrint("mnbudget","CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }

    // RETRIEVE CONFIRMATIONS AND NTIME
    /*
        - nTime starts as zero and is passed-by-reference out of this function and stored in the external proposal
        - nTime is never validated via the hashing mechanism and comes from a full-validated source (the blockchain)
    */

    int conf = GetIXConfirmations(nTxCollateralHash);
    if (nBlockHash != uint256(0)) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                conf += chainActive.Height() - pindex->nHeight + 1;
                nTime = pindex->nTime;
            }
        }
    }

    nConf = conf;

    //if we're syncing we won't have swiftTX information, so accept 1 confirmation
    if (conf >= Params().Budget_Fee_Confirmations()) {
        return true;
    } else {
        strError = strprintf("Collateral requires at least %d confirmations - %d confirmations", Params().Budget_Fee_Confirmations(), conf);
        LogPrint("mnbudget","CBudgetProposalBroadcast::IsBudgetCollateralValid - %s - %d confirmations\n", strError, conf);
        return false;
    }
}

void CBudgetManager::CheckOrphanVotes()
{
    LOCK(cs);


    std::string strError = "";
    std::map<uint256, CBudgetVote>::iterator it1 = mapOrphanMasternodeBudgetVotes.begin();
    while (it1 != mapOrphanMasternodeBudgetVotes.end()) {
        if (budget.UpdateProposal(((*it1).second), NULL, strError)) {
            LogPrint("mnbudget","CBudgetManager::CheckOrphanVotes - Proposal/Budget is known, activating and removing orphan vote\n");
            mapOrphanMasternodeBudgetVotes.erase(it1++);
        } else {
            ++it1;
        }
    }
    std::map<uint256, CFinalizedBudgetVote>::iterator it2 = mapOrphanFinalizedBudgetVotes.begin();
    while (it2 != mapOrphanFinalizedBudgetVotes.end()) {
        if (budget.UpdateFinalizedBudget(((*it2).second), NULL, strError)) {
            LogPrint("mnbudget","CBudgetManager::CheckOrphanVotes - Proposal/Budget is known, activating and removing orphan vote\n");
            mapOrphanFinalizedBudgetVotes.erase(it2++);
        } else {
            ++it2;
        }
    }
    LogPrint("mnbudget","CBudgetManager::CheckOrphanVotes - Done\n");
}

void CBudgetManager::SubmitFinalBudget()
{
    static int nSubmittedHeight = 0; // height at which final budget was submitted last time
    int nCurrentHeight;

    {
        TRY_LOCK(cs_main, locked);
        if (!locked) return;
        if (!chainActive.Tip()) return;
        nCurrentHeight = chainActive.Height();
    }

    int nBlockStart = nCurrentHeight - nCurrentHeight % Params().GetBudgetCycleBlocks() + Params().GetBudgetCycleBlocks();
    if (nSubmittedHeight >= nBlockStart){
        LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - nSubmittedHeight(=%ld) < nBlockStart(=%ld) condition not fulfilled.\n", nSubmittedHeight, nBlockStart);
        return;
    }

     // Submit final budget during the last 2 days (2880 blocks) before payment for Mainnet, about 9 minutes (9 blocks) for Testnet
    int finalizationWindow = ((Params().GetBudgetCycleBlocks() / 30) * 2);

    if (Params().NetworkID() == CBaseChainParams::TESTNET) {
        // NOTE: 9 blocks for testnet is way to short to have any masternode submit an automatic vote on the finalized(!) budget,
        //       because those votes are only submitted/relayed once every 56 blocks in CFinalizedBudget::AutoCheck()

        finalizationWindow = 64; // 56 + 4 finalization confirmations + 4 minutes buffer for propagation
    }

    int nFinalizationStart = nBlockStart - finalizationWindow;

    int nOffsetToStart = nFinalizationStart - nCurrentHeight;

    if (nBlockStart - nCurrentHeight > finalizationWindow) {
        LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - Too early for finalization. Current block is %ld, next Superblock is %ld.\n", nCurrentHeight, nBlockStart);
        LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - First possible block for finalization: %ld. Last possible block for finalization: %ld. You have to wait for %ld block(s) until Budget finalization will be possible\n", nFinalizationStart, nBlockStart, nOffsetToStart);

        return;
    }

    std::vector<CBudgetProposal*> vBudgetProposals = budget.GetBudget();
    std::string strBudgetName = "main";
    std::vector<CTxBudgetPayment> vecTxBudgetPayments;

    for (unsigned int i = 0; i < vBudgetProposals.size(); i++) {
        CTxBudgetPayment txBudgetPayment;
        txBudgetPayment.nProposalHash = vBudgetProposals[i]->GetHash();
        txBudgetPayment.payee = vBudgetProposals[i]->GetPayee();
        txBudgetPayment.nAmount = vBudgetProposals[i]->GetAllotted();
        vecTxBudgetPayments.push_back(txBudgetPayment);
    }

    if (vecTxBudgetPayments.size() < 1) {
        LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - Found No Proposals For Period\n");
        return;
    }

    CFinalizedBudgetBroadcast tempBudget(strBudgetName, nBlockStart, vecTxBudgetPayments, 0);
    if (mapSeenFinalizedBudgets.count(tempBudget.GetHash())) {
        LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - Budget already exists - %s\n", tempBudget.GetHash().ToString());
        nSubmittedHeight = nCurrentHeight;
        return; //already exists
    }

    //create fee tx
    CTransaction tx;
    uint256 txidCollateral;

    if (!mapCollateralTxids.count(tempBudget.GetHash())) {
        CWalletTx wtx;
        if (!pwalletMain->GetBudgetFinalizationCollateralTX(wtx, tempBudget.GetHash(), false)) {
            LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - Can't make collateral transaction\n");
            return;
        }

        // Get our change address
        CReserveKey reservekey(pwalletMain);
        // Send the tx to the network. Do NOT use SwiftTx, locking might need too much time to propagate, especially for testnet
        pwalletMain->CommitTransaction(wtx, reservekey, "NO-ix");
        tx = (CTransaction)wtx;
        txidCollateral = tx.GetHash();
        mapCollateralTxids.insert(make_pair(tempBudget.GetHash(), txidCollateral));
    } else {
        txidCollateral = mapCollateralTxids[tempBudget.GetHash()];
    }

    int conf = GetIXConfirmations(txidCollateral);
    CTransaction txCollateral;
    uint256 nBlockHash;

    if (!GetTransaction(txidCollateral, txCollateral, nBlockHash, true)) {
        LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - Can't find collateral tx %s", txidCollateral.ToString());
        return;
    }

    if (nBlockHash != uint256(0)) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                conf += chainActive.Height() - pindex->nHeight + 1;
            }
        }
    }

    /*
        Wait will we have 1 extra confirmation, otherwise some clients might reject this feeTX
        -- This function is tied to NewBlock, so we will propagate this budget while the block is also propagating
    */
    if (conf < Params().Budget_Fee_Confirmations() + 1) {
        LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - Collateral requires at least %d confirmations - %s - %d confirmations\n", Params().Budget_Fee_Confirmations() + 1, txidCollateral.ToString(), conf);
        return;
    }

    //create the proposal incase we're the first to make it
    CFinalizedBudgetBroadcast finalizedBudgetBroadcast(strBudgetName, nBlockStart, vecTxBudgetPayments, txidCollateral);

    std::string strError = "";
    if (!finalizedBudgetBroadcast.IsValid(strError)) {
        LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - Invalid finalized budget - %s \n", strError);
        return;
    }

    LOCK(cs);
    mapSeenFinalizedBudgets.insert(make_pair(finalizedBudgetBroadcast.GetHash(), finalizedBudgetBroadcast));
    finalizedBudgetBroadcast.Relay();
    budget.AddFinalizedBudget(finalizedBudgetBroadcast);
    nSubmittedHeight = nCurrentHeight;
    LogPrint("mnbudget","CBudgetManager::SubmitFinalBudget - Done! %s\n", finalizedBudgetBroadcast.GetHash().ToString());
}

//
// CBudgetDB
//

CBudgetDB::CBudgetDB()
{
    pathDB = GetDataDir() / "budget.dat";
    strMagicMessage = "MasternodeBudget";
}

bool CBudgetDB::Write(const CBudgetManager& objToSave)
{
    LOCK(objToSave.cs);

    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // masternode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("mnbudget","Written info to budget.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CBudgetDB::ReadResult CBudgetDB::Read(CBudgetManager& objToLoad, bool fDryRun)
{
    LOCK(objToLoad.cs);

    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }


    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CBudgetManager object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("mnbudget","Loaded info from budget.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("mnbudget","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("mnbudget","Budget manager - cleaning....\n");
        objToLoad.CheckAndRemove();
        LogPrint("mnbudget","Budget manager - result:\n");
        LogPrint("mnbudget","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpBudgets()
{
    int64_t nStart = GetTimeMillis();

    CBudgetDB budgetdb;
    CBudgetManager tempBudget;

    LogPrint("mnbudget","Verifying budget.dat format...\n");
    CBudgetDB::ReadResult readResult = budgetdb.Read(tempBudget, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CBudgetDB::FileError)
        LogPrint("mnbudget","Missing budgets file - budget.dat, will try to recreate\n");
    else if (readResult != CBudgetDB::Ok) {
        LogPrint("mnbudget","Error reading budget.dat: ");
        if (readResult == CBudgetDB::IncorrectFormat)
            LogPrint("mnbudget","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("mnbudget","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("mnbudget","Writting info to budget.dat...\n");
    budgetdb.Write(budget);

    LogPrint("mnbudget","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool CBudgetManager::AddFinalizedBudget(CFinalizedBudget& finalizedBudget)
{
    std::string strError = "";
    if (!finalizedBudget.IsValid(strError)) return false;

    if (mapFinalizedBudgets.count(finalizedBudget.GetHash())) {
        return false;
    }

    mapFinalizedBudgets.insert(make_pair(finalizedBudget.GetHash(), finalizedBudget));
    return true;
}

bool CBudgetManager::AddProposal(CBudgetProposal& budgetProposal)
{
    LOCK(cs);
    std::string strError = "";
    if (!budgetProposal.IsValid(strError)) {
        LogPrint("mnbudget","CBudgetManager::AddProposal - invalid budget proposal - %s\n", strError);
        return false;
    }

    if (mapProposals.count(budgetProposal.GetHash())) {
        return false;
    }

    mapProposals.insert(make_pair(budgetProposal.GetHash(), budgetProposal));
    LogPrint("mnbudget","CBudgetManager::AddProposal - proposal %s added\n", budgetProposal.GetName ().c_str ());
    return true;
}

void CBudgetManager::CheckAndRemove()
{
    int nHeight = 0;

    // Add some verbosity once loading blocks from files has finished
    {
        TRY_LOCK(cs_main, locked);
        if ((locked) && (chainActive.Tip() != NULL)) {
            CBlockIndex* pindexPrev = chainActive.Tip();
            if (pindexPrev) {
                nHeight = pindexPrev->nHeight;
            }
        }
    }

    LogPrint("mnbudget", "CBudgetManager::CheckAndRemove at Height=%d\n", nHeight);

    map<uint256, CFinalizedBudget> tmpMapFinalizedBudgets;
    map<uint256, CBudgetProposal> tmpMapProposals;

    std::string strError = "";

    LogPrint("mnbudget", "CBudgetManager::CheckAndRemove - mapFinalizedBudgets cleanup - size before: %d\n", mapFinalizedBudgets.size());
    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);

        pfinalizedBudget->fValid = pfinalizedBudget->IsValid(strError);
        if (!strError.empty ()) {
            LogPrint("mnbudget","CBudgetManager::CheckAndRemove - Invalid finalized budget: %s\n", strError);
        }
        else {
            LogPrint("mnbudget","CBudgetManager::CheckAndRemove - Found valid finalized budget: %s %s\n",
                      pfinalizedBudget->strBudgetName.c_str(), pfinalizedBudget->nFeeTXHash.ToString().c_str());
        }

        if (pfinalizedBudget->fValid) {
            pfinalizedBudget->CheckAndVote();
            tmpMapFinalizedBudgets.insert(make_pair(pfinalizedBudget->GetHash(), *pfinalizedBudget));
        }

        ++it;
    }

    LogPrint("mnbudget", "CBudgetManager::CheckAndRemove - mapProposals cleanup - size before: %d\n", mapProposals.size());
    std::map<uint256, CBudgetProposal>::iterator it2 = mapProposals.begin();
    while (it2 != mapProposals.end()) {
        CBudgetProposal* pbudgetProposal = &((*it2).second);
        pbudgetProposal->fValid = pbudgetProposal->IsValid(strError);
        if (!strError.empty ()) {
            LogPrint("mnbudget","CBudgetManager::CheckAndRemove - Invalid budget proposal - %s\n", strError);
            strError = "";
        }
        else {
             LogPrint("mnbudget","CBudgetManager::CheckAndRemove - Found valid budget proposal: %s %s\n",
                      pbudgetProposal->strProposalName.c_str(), pbudgetProposal->nFeeTXHash.ToString().c_str());
        }
        if (pbudgetProposal->fValid) {
            tmpMapProposals.insert(make_pair(pbudgetProposal->GetHash(), *pbudgetProposal));
        }

        ++it2;
    }
    // Remove invalid entries by overwriting complete map
    mapFinalizedBudgets.swap(tmpMapFinalizedBudgets);
    mapProposals.swap(tmpMapProposals);

    // clang doesn't accept copy assignemnts :-/
    // mapFinalizedBudgets = tmpMapFinalizedBudgets;
    // mapProposals = tmpMapProposals;

    LogPrint("mnbudget", "CBudgetManager::CheckAndRemove - mapFinalizedBudgets cleanup - size after: %d\n", mapFinalizedBudgets.size());
    LogPrint("mnbudget", "CBudgetManager::CheckAndRemove - mapProposals cleanup - size after: %d\n", mapProposals.size());
    LogPrint("mnbudget","CBudgetManager::CheckAndRemove - PASSED\n");

}

CAmount CBudgetManager::FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake)
{
    LOCK(cs);

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return 0;

    int nHighestCount = 0;
    CScript payee;
    CAmount nAmount = 0;

    // ------- Grab The Highest Count

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if (pfinalizedBudget->GetVoteCount() > nHighestCount &&
            pindexPrev->nHeight + 1 >= pfinalizedBudget->GetBlockStart() &&
            pindexPrev->nHeight + 1 <= pfinalizedBudget->GetBlockEnd() &&
            pfinalizedBudget->GetPayeeAndAmount(pindexPrev->nHeight + 1, payee, nAmount)) {
            nHighestCount = pfinalizedBudget->GetVoteCount();
        }

        ++it;
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nHeight);

    if (fProofOfStake) {
        if (nHighestCount > 0) {
            unsigned int i = txNew.vout.size();
            txNew.vout.resize(i + 1);
            txNew.vout[i].scriptPubKey = payee;
            txNew.vout[i].nValue = nAmount;

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);
            LogPrint("mnbudget","CBudgetManager::FillBlockPayee - Budget payment to %s for %lld, nHighestCount = %d\n", address2.ToString(), nAmount, nHighestCount);
        }
        else {
            LogPrint("mnbudget","CBudgetManager::FillBlockPayee - No Budget payment, nHighestCount = %d\n", nHighestCount);
        }
    } else {
        //miners get the full amount on these blocks
        txNew.vout[0].nValue = blockValue;

        if (nHighestCount > 0) {
            txNew.vout.resize(2);

            //these are super blocks, so their value can be much larger than normal
            txNew.vout[1].scriptPubKey = payee;
            txNew.vout[1].nValue = nAmount;

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrint("mnbudget","CBudgetManager::FillBlockPayee - Budget payment to %s for %lld\n", address2.ToString(), nAmount);
        }
    }

    return nAmount;
}

CFinalizedBudget* CBudgetManager::FindFinalizedBudget(uint256 nHash)
{
    if (mapFinalizedBudgets.count(nHash))
        return &mapFinalizedBudgets[nHash];

    return NULL;
}

CBudgetProposal* CBudgetManager::FindProposal(const std::string& strProposalName)
{
    //find the prop with the highest yes count

    int nYesCount = -99999;
    CBudgetProposal* pbudgetProposal = NULL;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while (it != mapProposals.end()) {
        if ((*it).second.strProposalName == strProposalName && (*it).second.GetYeas() > nYesCount) {
            pbudgetProposal = &((*it).second);
            nYesCount = pbudgetProposal->GetYeas();
        }
        ++it;
    }

    if (nYesCount == -99999) return NULL;

    return pbudgetProposal;
}

CBudgetProposal* CBudgetManager::FindProposal(uint256 nHash)
{
    LOCK(cs);

    if (mapProposals.count(nHash))
        return &mapProposals[nHash];

    return NULL;
}

bool CBudgetManager::IsBudgetPaymentBlock(int nBlockHeight)
{
    int nHighestCount = -1;
    int nFivePercent = mnodeman.CountEnabled(ActiveProtocol()) / 20;

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if (pfinalizedBudget->GetVoteCount() > nHighestCount &&
            nBlockHeight >= pfinalizedBudget->GetBlockStart() &&
            nBlockHeight <= pfinalizedBudget->GetBlockEnd()) {
            nHighestCount = pfinalizedBudget->GetVoteCount();
        }

        ++it;
    }

    LogPrint("mnbudget","CBudgetManager::IsBudgetPaymentBlock() - nHighestCount: %lli, 5%% of Masternodes: %lli. Number of finalized budgets: %lli\n",
              nHighestCount, nFivePercent, mapFinalizedBudgets.size());

    // If budget doesn't have 5% of the network votes, then we should pay a masternode instead
    if (nHighestCount > nFivePercent) return true;

    return false;
}

TrxValidationStatus CBudgetManager::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs);

    TrxValidationStatus transactionStatus = TrxValidationStatus::InValid;
    int nHighestCount = 0;
    int nFivePercent = mnodeman.CountEnabled(ActiveProtocol()) / 20;
    std::vector<CFinalizedBudget*> ret;

    LogPrint("mnbudget","CBudgetManager::IsTransactionValid - checking %lli finalized budgets\n", mapFinalizedBudgets.size());

    // ------- Grab The Highest Count

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);

        if (pfinalizedBudget->GetVoteCount() > nHighestCount &&
            nBlockHeight >= pfinalizedBudget->GetBlockStart() &&
            nBlockHeight <= pfinalizedBudget->GetBlockEnd()) {
            nHighestCount = pfinalizedBudget->GetVoteCount();
        }

        ++it;
    }

    LogPrint("mnbudget","CBudgetManager::IsTransactionValid() - nHighestCount: %lli, 5%% of Masternodes: %lli mapFinalizedBudgets.size(): %ld\n",
              nHighestCount, nFivePercent, mapFinalizedBudgets.size());
    /*
        If budget doesn't have 5% of the network votes, then we should pay a masternode instead
    */
    if (nHighestCount < nFivePercent) return TrxValidationStatus::InValid;

    // check the highest finalized budgets (+/- 10% to assist in consensus)

    std::string strProposals = "";
    int nCountThreshold = nHighestCount - mnodeman.CountEnabled(ActiveProtocol()) / 10;
    bool fThreshold = false;
    it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        strProposals = pfinalizedBudget->GetProposals();

        LogPrint("mnbudget","CBudgetManager::IsTransactionValid - checking budget (%s) with blockstart %lli, blockend %lli, nBlockHeight %lli, votes %lli, nCountThreshold %lli\n",
                 strProposals.c_str(), pfinalizedBudget->GetBlockStart(), pfinalizedBudget->GetBlockEnd(),
                 nBlockHeight, pfinalizedBudget->GetVoteCount(), nCountThreshold);

        if (pfinalizedBudget->GetVoteCount() > nCountThreshold) {
            fThreshold = true;
            LogPrint("mnbudget","CBudgetManager::IsTransactionValid - GetVoteCount() > nCountThreshold passed\n");
            if (nBlockHeight >= pfinalizedBudget->GetBlockStart() && nBlockHeight <= pfinalizedBudget->GetBlockEnd()) {
                LogPrint("mnbudget","CBudgetManager::IsTransactionValid - GetBlockStart() passed\n");
                transactionStatus = pfinalizedBudget->IsTransactionValid(txNew, nBlockHeight);
                if (transactionStatus == TrxValidationStatus::Valid) {
                    LogPrint("mnbudget","CBudgetManager::IsTransactionValid - pfinalizedBudget->IsTransactionValid() passed\n");
                    return TrxValidationStatus::Valid;
                }
                else {
                    LogPrint("mnbudget","CBudgetManager::IsTransactionValid - pfinalizedBudget->IsTransactionValid() error\n");
                }
            }
            else {
                LogPrint("mnbudget","CBudgetManager::IsTransactionValid - GetBlockStart() failed, budget is outside current payment cycle and will be ignored.\n");
            }

        }

        ++it;
    }

    // If not enough masternodes autovoted for any of the finalized budgets pay a masternode instead
    if(!fThreshold) {
        transactionStatus = TrxValidationStatus::VoteThreshold;
    }

    // We looked through all of the known budgets
    return transactionStatus;
}

std::vector<CBudgetProposal*> CBudgetManager::GetAllProposals()
{
    LOCK(cs);

    std::vector<CBudgetProposal*> vBudgetProposalRet;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while (it != mapProposals.end()) {
        (*it).second.CleanAndRemove(false);

        CBudgetProposal* pbudgetProposal = &((*it).second);
        vBudgetProposalRet.push_back(pbudgetProposal);

        ++it;
    }

    return vBudgetProposalRet;
}

//
// Sort by votes, if there's a tie sort by their feeHash TX
//
struct sortProposalsByVotes {
    bool operator()(const std::pair<CBudgetProposal*, int>& left, const std::pair<CBudgetProposal*, int>& right)
    {
        if (left.second != right.second)
            return (left.second > right.second);
        return (left.first->nFeeTXHash > right.first->nFeeTXHash);
    }
};

//Need to review this function
std::vector<CBudgetProposal*> CBudgetManager::GetBudget()
{
    LOCK(cs);

    // ------- Sort budgets by Yes Count

    std::vector<std::pair<CBudgetProposal*, int> > vBudgetPorposalsSort;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while (it != mapProposals.end()) {
        (*it).second.CleanAndRemove(false);
        vBudgetPorposalsSort.push_back(make_pair(&((*it).second), (*it).second.GetYeas() - (*it).second.GetNays()));
        ++it;
    }

    std::sort(vBudgetPorposalsSort.begin(), vBudgetPorposalsSort.end(), sortProposalsByVotes());

    // ------- Grab The Budgets In Order

    std::vector<CBudgetProposal*> vBudgetProposalsRet;

    CAmount nBudgetAllocated = 0;

    CBlockIndex* pindexPrev;
    {
        LOCK(cs_main);
        pindexPrev = chainActive.Tip();
    }
    if (pindexPrev == NULL) return vBudgetProposalsRet;

    int nBlockStart = pindexPrev->nHeight - pindexPrev->nHeight % Params().GetBudgetCycleBlocks() + Params().GetBudgetCycleBlocks();
    int nBlockEnd = nBlockStart + Params().GetBudgetCycleBlocks() - 1;
    int mnCount = mnodeman.CountEnabled(ActiveProtocol());
    CAmount nTotalBudget = GetTotalBudget(nBlockStart);

    std::vector<std::pair<CBudgetProposal*, int> >::iterator it2 = vBudgetPorposalsSort.begin();
    while (it2 != vBudgetPorposalsSort.end()) {
        CBudgetProposal* pbudgetProposal = (*it2).first;

        LogPrint("mnbudget","CBudgetManager::GetBudget() - Processing Budget %s\n", pbudgetProposal->strProposalName.c_str());
        //prop start/end should be inside this period
        if (pbudgetProposal->IsPassing(pindexPrev, nBlockStart, nBlockEnd, mnCount)) {
            LogPrint("mnbudget","CBudgetManager::GetBudget() -   Check 1 passed: valid=%d | %ld <= %ld | %ld >= %ld | Yeas=%d Nays=%d Count=%d | established=%d\n",
                      pbudgetProposal->fValid, pbudgetProposal->nBlockStart, nBlockStart, pbudgetProposal->nBlockEnd,
                      nBlockEnd, pbudgetProposal->GetYeas(), pbudgetProposal->GetNays(), mnCount / 10,
                      pbudgetProposal->IsEstablished());

            if (pbudgetProposal->GetAmount() + nBudgetAllocated <= nTotalBudget) {
                pbudgetProposal->SetAllotted(pbudgetProposal->GetAmount());
                nBudgetAllocated += pbudgetProposal->GetAmount();
                vBudgetProposalsRet.push_back(pbudgetProposal);
                LogPrint("mnbudget","CBudgetManager::GetBudget() -     Check 2 passed: Budget added\n");
            } else {
                pbudgetProposal->SetAllotted(0);
                LogPrint("mnbudget","CBudgetManager::GetBudget() -     Check 2 failed: no amount allotted\n");
            }
        }
        else {
            LogPrint("mnbudget","CBudgetManager::GetBudget() -   Check 1 failed: valid=%d | %ld <= %ld | %ld >= %ld | Yeas=%d Nays=%d Count=%d | established=%d\n",
                      pbudgetProposal->fValid, pbudgetProposal->nBlockStart, nBlockStart, pbudgetProposal->nBlockEnd,
                      nBlockEnd, pbudgetProposal->GetYeas(), pbudgetProposal->GetNays(), mnodeman.CountEnabled(ActiveProtocol()) / 10,
                      pbudgetProposal->IsEstablished());
        }

        ++it2;
    }

    return vBudgetProposalsRet;
}

// Sort by votes, if there's a tie sort by their feeHash TX
struct sortFinalizedBudgetsByVotes {
    bool operator()(const std::pair<CFinalizedBudget*, int>& left, const std::pair<CFinalizedBudget*, int>& right)
    {
        if (left.second != right.second)
            return left.second > right.second;
        return (left.first->nFeeTXHash > right.first->nFeeTXHash);
    }
};

std::vector<CFinalizedBudget*> CBudgetManager::GetFinalizedBudgets()
{
    LOCK(cs);

    std::vector<CFinalizedBudget*> vFinalizedBudgetsRet;
    std::vector<std::pair<CFinalizedBudget*, int> > vFinalizedBudgetsSort;

    // ------- Grab The Budgets In Order

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);

        vFinalizedBudgetsSort.push_back(make_pair(pfinalizedBudget, pfinalizedBudget->GetVoteCount()));
        ++it;
    }
    std::sort(vFinalizedBudgetsSort.begin(), vFinalizedBudgetsSort.end(), sortFinalizedBudgetsByVotes());

    std::vector<std::pair<CFinalizedBudget*, int> >::iterator it2 = vFinalizedBudgetsSort.begin();
    while (it2 != vFinalizedBudgetsSort.end()) {
        vFinalizedBudgetsRet.push_back((*it2).first);
        ++it2;
    }

    return vFinalizedBudgetsRet;
}

std::string CBudgetManager::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs);

    std::string ret = "unknown-budget";

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if (nBlockHeight >= pfinalizedBudget->GetBlockStart() && nBlockHeight <= pfinalizedBudget->GetBlockEnd()) {
            CTxBudgetPayment payment;
            if (pfinalizedBudget->GetBudgetPaymentByBlock(nBlockHeight, payment)) {
                if (ret == "unknown-budget") {
                    ret = payment.nProposalHash.ToString();
                } else {
                    ret += ",";
                    ret += payment.nProposalHash.ToString();
                }
            } else {
                LogPrint("mnbudget","CBudgetManager::GetRequiredPaymentsString - Couldn't find budget payment for block %d\n", nBlockHeight);
            }
        }

        ++it;
    }

    return ret;
}

CAmount CBudgetManager::GetTotalBudget(int nHeight)
{
    if (chainActive.Tip() == NULL) return 0;

    CAmount nSubsidy = 0;
    if (Params().NetworkID() == CBaseChainParams::TESTNET) {
        nSubsidy = 5000 * COIN;
    } else {
        nSubsidy = 100 * COIN;
    }
    return ((nSubsidy / 100) * 10) * Params().GetBudgetCycleBlocks();
}

void CBudgetManager::NewBlock()
{
    TRY_LOCK(cs, fBudgetNewBlock);
    if (!fBudgetNewBlock) return;

    if (masternodeSync.RequestedMasternodeAssets <= MASTERNODE_SYNC_BUDGET) return;

    if (strBudgetMode == "suggest") { //suggest the budget we see
        SubmitFinalBudget();
    }

    //this function should be called 1/14 blocks, allowing up to 100 votes per day on all proposals
    if (chainActive.Height() % 14 != 0) return;

    // incremental sync with our peers
    if (masternodeSync.IsSynced()) {
        LogPrint("mnbudget","CBudgetManager::NewBlock - incremental sync started\n");
        if (chainActive.Height() % 1440 == rand() % 1440) {
            ClearSeen();
            ResetSync();
        }

        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes)
            if (pnode->nVersion >= ActiveProtocol())
                Sync(pnode, 0, true);

        MarkSynced();
    }


    CheckAndRemove();

    //remove invalid votes once in a while (we have to check the signatures and validity of every vote, somewhat CPU intensive)

    LogPrint("mnbudget","CBudgetManager::NewBlock - askedForSourceProposalOrBudget cleanup - size: %d\n", askedForSourceProposalOrBudget.size());
    std::map<uint256, int64_t>::iterator it = askedForSourceProposalOrBudget.begin();
    while (it != askedForSourceProposalOrBudget.end()) {
        if ((*it).second > GetTime() - (60 * 60 * 24)) {
            ++it;
        } else {
            askedForSourceProposalOrBudget.erase(it++);
        }
    }

    LogPrint("mnbudget","CBudgetManager::NewBlock - mapProposals cleanup - size: %d\n", mapProposals.size());
    std::map<uint256, CBudgetProposal>::iterator it2 = mapProposals.begin();
    while (it2 != mapProposals.end()) {
        (*it2).second.CleanAndRemove(false);
        ++it2;
    }

    LogPrint("mnbudget","CBudgetManager::NewBlock - mapFinalizedBudgets cleanup - size: %d\n", mapFinalizedBudgets.size());
    std::map<uint256, CFinalizedBudget>::iterator it3 = mapFinalizedBudgets.begin();
    while (it3 != mapFinalizedBudgets.end()) {
        (*it3).second.CleanAndRemove(false);
        ++it3;
    }

    LogPrint("mnbudget","CBudgetManager::NewBlock - vecImmatureBudgetProposals cleanup - size: %d\n", vecImmatureBudgetProposals.size());
    std::vector<CBudgetProposalBroadcast>::iterator it4 = vecImmatureBudgetProposals.begin();
    while (it4 != vecImmatureBudgetProposals.end()) {
        std::string strError = "";
        int nConf = 0;
        if (!IsBudgetCollateralValid((*it4).nFeeTXHash, (*it4).GetHash(), strError, (*it4).nTime, nConf)) {
            ++it4;
            continue;
        }

        if (!(*it4).IsValid(strError)) {
            LogPrint("mnbudget","mprop (immature) - invalid budget proposal - %s\n", strError);
            it4 = vecImmatureBudgetProposals.erase(it4);
            continue;
        }

        CBudgetProposal budgetProposal((*it4));
        if (AddProposal(budgetProposal)) {
            (*it4).Relay();
        }

        LogPrint("mnbudget","mprop (immature) - new budget - %s\n", (*it4).GetHash().ToString());
        it4 = vecImmatureBudgetProposals.erase(it4);
    }

    LogPrint("mnbudget","CBudgetManager::NewBlock - vecImmatureFinalizedBudgets cleanup - size: %d\n", vecImmatureFinalizedBudgets.size());
    std::vector<CFinalizedBudgetBroadcast>::iterator it5 = vecImmatureFinalizedBudgets.begin();
    while (it5 != vecImmatureFinalizedBudgets.end()) {
        std::string strError = "";
        int nConf = 0;
        if (!IsBudgetCollateralValid((*it5).nFeeTXHash, (*it5).GetHash(), strError, (*it5).nTime, nConf, true)) {
            ++it5;
            continue;
        }

        if (!(*it5).IsValid(strError)) {
            LogPrint("mnbudget","fbs (immature) - invalid finalized budget - %s\n", strError);
            it5 = vecImmatureFinalizedBudgets.erase(it5);
            continue;
        }

        LogPrint("mnbudget","fbs (immature) - new finalized budget - %s\n", (*it5).GetHash().ToString());

        CFinalizedBudget finalizedBudget((*it5));
        if (AddFinalizedBudget(finalizedBudget)) {
            (*it5).Relay();
        }

        it5 = vecImmatureFinalizedBudgets.erase(it5);
    }
    LogPrint("mnbudget","CBudgetManager::NewBlock - PASSED\n");
}

void CBudgetManager::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // lite mode is not supported
    if (fLiteMode) return;
    if (!masternodeSync.IsBlockchainSynced()) return;

    LOCK(cs_budget);

    if (strCommand == "mnvs") { //Masternode vote sync
        uint256 nProp;
        vRecv >> nProp;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (nProp == 0) {
                if (pfrom->HasFulfilledRequest("mnvs")) {
                    LogPrint("mnbudget","mnvs - peer already asked me for the list\n");
                    Misbehaving(pfrom->GetId(), 20);
                    return;
                }
                pfrom->FulfilledRequest("mnvs");
            }
        }

        Sync(pfrom, nProp);
        LogPrint("mnbudget", "mnvs - Sent Masternode votes to peer %i\n", pfrom->GetId());
    }

    if (strCommand == "mprop") { //Masternode Proposal
        CBudgetProposalBroadcast budgetProposalBroadcast;
        vRecv >> budgetProposalBroadcast;

        if (mapSeenMasternodeBudgetProposals.count(budgetProposalBroadcast.GetHash())) {
            masternodeSync.AddedBudgetItem(budgetProposalBroadcast.GetHash());
            return;
        }

        std::string strError = "";
        int nConf = 0;
        if (!IsBudgetCollateralValid(budgetProposalBroadcast.nFeeTXHash, budgetProposalBroadcast.GetHash(), strError, budgetProposalBroadcast.nTime, nConf)) {
            LogPrint("mnbudget","Proposal FeeTX is not valid - %s - %s\n", budgetProposalBroadcast.nFeeTXHash.ToString(), strError);
            if (nConf >= 1) vecImmatureBudgetProposals.push_back(budgetProposalBroadcast);
            return;
        }

        mapSeenMasternodeBudgetProposals.insert(make_pair(budgetProposalBroadcast.GetHash(), budgetProposalBroadcast));

        if (!budgetProposalBroadcast.IsValid(strError)) {
            LogPrint("mnbudget","mprop - invalid budget proposal - %s\n", strError);
            return;
        }

        CBudgetProposal budgetProposal(budgetProposalBroadcast);
        if (AddProposal(budgetProposal)) {
            budgetProposalBroadcast.Relay();
        }
        masternodeSync.AddedBudgetItem(budgetProposalBroadcast.GetHash());

        LogPrint("mnbudget","mprop - new budget - %s\n", budgetProposalBroadcast.GetHash().ToString());

        //We might have active votes for this proposal that are valid now
        CheckOrphanVotes();
    }

    if (strCommand == "mvote") { //Masternode Vote
        CBudgetVote vote;
        vRecv >> vote;
        vote.fValid = true;

        if (mapSeenMasternodeBudgetVotes.count(vote.GetHash())) {
            masternodeSync.AddedBudgetItem(vote.GetHash());
            return;
        }

        CMasternode* pmn = mnodeman.Find(vote.vin);
        if (pmn == NULL) {
            LogPrint("mnbudget","mvote - unknown masternode - vin: %s\n", vote.vin.prevout.hash.ToString());
            mnodeman.AskForMN(pfrom, vote.vin);
            return;
        }


        mapSeenMasternodeBudgetVotes.insert(make_pair(vote.GetHash(), vote));
        if (!vote.SignatureValid(true)) {
            if (masternodeSync.IsSynced()) {
                LogPrintf("CBudgetManager::ProcessMessage() : mvote - signature invalid\n");
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, vote.vin);
            return;
        }

        std::string strError = "";
        if (UpdateProposal(vote, pfrom, strError)) {
            vote.Relay();
            masternodeSync.AddedBudgetItem(vote.GetHash());
        }

        LogPrint("mnbudget","mvote - new budget vote for budget %s - %s\n", vote.nProposalHash.ToString(),  vote.GetHash().ToString());
    }

    if (strCommand == "fbs") { //Finalized Budget Suggestion
        CFinalizedBudgetBroadcast finalizedBudgetBroadcast;
        vRecv >> finalizedBudgetBroadcast;

        if (mapSeenFinalizedBudgets.count(finalizedBudgetBroadcast.GetHash())) {
            masternodeSync.AddedBudgetItem(finalizedBudgetBroadcast.GetHash());
            return;
        }

        std::string strError = "";
        int nConf = 0;
        if (!IsBudgetCollateralValid(finalizedBudgetBroadcast.nFeeTXHash, finalizedBudgetBroadcast.GetHash(), strError, finalizedBudgetBroadcast.nTime, nConf, true)) {
            LogPrint("mnbudget","fbs - Finalized Budget FeeTX is not valid - %s - %s\n", finalizedBudgetBroadcast.nFeeTXHash.ToString(), strError);

            if (nConf >= 1) vecImmatureFinalizedBudgets.push_back(finalizedBudgetBroadcast);
            return;
        }

        mapSeenFinalizedBudgets.insert(make_pair(finalizedBudgetBroadcast.GetHash(), finalizedBudgetBroadcast));

        if (!finalizedBudgetBroadcast.IsValid(strError)) {
            LogPrint("mnbudget","fbs - invalid finalized budget - %s\n", strError);
            return;
        }

        LogPrint("mnbudget","fbs - new finalized budget - %s\n", finalizedBudgetBroadcast.GetHash().ToString());

        CFinalizedBudget finalizedBudget(finalizedBudgetBroadcast);
        if (AddFinalizedBudget(finalizedBudget)) {
            finalizedBudgetBroadcast.Relay();
        }
        masternodeSync.AddedBudgetItem(finalizedBudgetBroadcast.GetHash());

        //we might have active votes for this budget that are now valid
        CheckOrphanVotes();
    }

    if (strCommand == "fbvote") { //Finalized Budget Vote
        CFinalizedBudgetVote vote;
        vRecv >> vote;
        vote.fValid = true;

        if (mapSeenFinalizedBudgetVotes.count(vote.GetHash())) {
            masternodeSync.AddedBudgetItem(vote.GetHash());
            return;
        }

        CMasternode* pmn = mnodeman.Find(vote.vin);
        if (pmn == NULL) {
            LogPrint("mnbudget", "fbvote - unknown masternode - vin: %s\n", vote.vin.prevout.hash.ToString());
            mnodeman.AskForMN(pfrom, vote.vin);
            return;
        }

        mapSeenFinalizedBudgetVotes.insert(make_pair(vote.GetHash(), vote));
        if (!vote.SignatureValid(true)) {
            if (masternodeSync.IsSynced()) {
                LogPrintf("CBudgetManager::ProcessMessage() : fbvote - signature from masternode %s invalid\n", HexStr(pmn->pubKeyMasternode));
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, vote.vin);
            return;
        }

        std::string strError = "";
        if (UpdateFinalizedBudget(vote, pfrom, strError)) {
            vote.Relay();
            masternodeSync.AddedBudgetItem(vote.GetHash());

            LogPrint("mnbudget","fbvote - new finalized budget vote - %s from masternode %s\n", vote.GetHash().ToString(), HexStr(pmn->pubKeyMasternode));
        } else {
            LogPrint("mnbudget","fbvote - rejected finalized budget vote - %s from masternode %s - %s\n", vote.GetHash().ToString(), HexStr(pmn->pubKeyMasternode), strError);
        }
    }
}

bool CBudgetManager::PropExists(uint256 nHash)
{
    if (mapProposals.count(nHash)) return true;
    return false;
}

//mark that a full sync is needed
void CBudgetManager::ResetSync()
{
    LOCK(cs);


    std::map<uint256, CBudgetProposalBroadcast>::iterator it1 = mapSeenMasternodeBudgetProposals.begin();
    while (it1 != mapSeenMasternodeBudgetProposals.end()) {
        CBudgetProposal* pbudgetProposal = FindProposal((*it1).first);
        if (pbudgetProposal && pbudgetProposal->fValid) {
            //mark votes
            std::map<uint256, CBudgetVote>::iterator it2 = pbudgetProposal->mapVotes.begin();
            while (it2 != pbudgetProposal->mapVotes.end()) {
                (*it2).second.fSynced = false;
                ++it2;
            }
        }
        ++it1;
    }

    std::map<uint256, CFinalizedBudgetBroadcast>::iterator it3 = mapSeenFinalizedBudgets.begin();
    while (it3 != mapSeenFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = FindFinalizedBudget((*it3).first);
        if (pfinalizedBudget && pfinalizedBudget->fValid) {
            //send votes
            std::map<uint256, CFinalizedBudgetVote>::iterator it4 = pfinalizedBudget->mapVotes.begin();
            while (it4 != pfinalizedBudget->mapVotes.end()) {
                (*it4).second.fSynced = false;
                ++it4;
            }
        }
        ++it3;
    }
}

void CBudgetManager::MarkSynced()
{
    LOCK(cs);

    /*
        Mark that we've sent all valid items
    */

    std::map<uint256, CBudgetProposalBroadcast>::iterator it1 = mapSeenMasternodeBudgetProposals.begin();
    while (it1 != mapSeenMasternodeBudgetProposals.end()) {
        CBudgetProposal* pbudgetProposal = FindProposal((*it1).first);
        if (pbudgetProposal && pbudgetProposal->fValid) {
            //mark votes