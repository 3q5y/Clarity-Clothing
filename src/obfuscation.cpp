// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "obfuscation.h"
#include "coincontrol.h"
#include "init.h"
#include "main.h"
#include "masternodeman.h"
#include "script/sign.h"
#include "swifttx.h"
#include "guiinterface.h"
#include "util.h"
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/foreach.hpp>

#include <algorithm>
#include <boost/assign/list_of.hpp>
#include <openssl/rand.h>

using namespace std;
using namespace boost;

// The main object for accessing Obfuscation
CObfuscationPool obfuScationPool;
// A helper object for signing messages from Masternodes
CObfuScationSigner obfuScationSigner;
// The current Obfuscations in progress on the network
std::vector<CObfuscationQueue> vecObfuscationQueue;
// Keep track of the used Masternodes
std::vector<CTxIn> vecMasternodesUsed;
// Keep track of the scanning errors I've seen
map<uint256, CObfuscationBroadcastTx> mapObfuscationBroadcastTxes;
// Keep track of the active Masternode
CActiveMasternode activeMasternode;

/* *** BEGIN OBFUSCATION MAGIC - GIC **********
    Copyright (c) 2014-2015, Dash Developers
        eduffield - evan@dashpay.io
        udjinm6   - udjinm6@dashpay.io
*/

void CObfuscationPool::ProcessMessageObfuscation(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Obfuscation/Masternode related functionality
    if (!masternodeSync.IsBlockchainSynced()) return;

    if (strCommand == "dsa") { //Obfuscation Accept Into Pool

        int errorID;

        if (pfrom->nVersion < ActiveProtocol()) {
            errorID = ERR_VERSION;
            LogPrintf("dsa -- incompatible version! \n");
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);

            return;
        }

        if (!fMasterNode) {
            errorID = ERR_NOT_A_MN;
            LogPrintf("dsa -- not a Masternode! \n");
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);

            return;
        }

        int nDenom;
        CTransaction txCollateral;
        vRecv >> nDenom >> txCollateral;

        CMasternode* pmn = mnodeman.Find(activeMasternode.vin);
        if (pmn == NULL) {
            errorID = ERR_MN_LIST;
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
            return;
        }

        if (sessionUsers == 0) {
            if (pmn->nLastDsq != 0 &&
                pmn->nLastDsq + mnodeman.CountEnabled(ActiveProtocol()) / 5 > mnodeman.nDsqCount) {
                LogPrintf("dsa -- last dsq too recent, must wait. %s \n", pfrom->addr.ToString());
                errorID = ERR_RECENT;
                pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
                return;
            }
        }

        if (!IsCompatibleWithSession(nDenom, txCollateral, errorID)) {
            LogPrintf("dsa -- not compatible with existing transactions! \n");
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
            return;
        } else {
            LogPrintf("dsa -- is compatible, please submit! \n");
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_ACCEPTED, errorID);
            return;
        }

    } else if (strCommand == "dsq") { //Obfuscation Queue
        TRY_LOCK(cs_obfuscation, lockRecv);
        if (!lockRecv) return;

        if (pfrom->nVersion < ActiveProtocol()) {
            return;
        }

        CObfuscationQueue dsq;
        vRecv >> dsq;

        CService addr;
        if (!dsq.GetAddress(addr)) return;
        if (!dsq.CheckSignature()) return;

        if (dsq.IsExpired()) return;

        CMasternode* pmn = mnodeman.Find(dsq.vin);
        if (pmn == NULL) return;

        // if the queue is ready, submit if we can
        if (dsq.ready) {
            if (!pSubmittedToMasternode) return;
            if ((CNetAddr)pSubmittedToMasternode->addr != (CNetAddr)addr) {
                LogPrintf("dsq - message doesn't match current Masternode - %s != %s\n", pSubmittedToMasternode->addr.ToString(), addr.ToString());
                return;
            }

            if (state == POOL_STATUS_QUEUE) {
                LogPrint("obfuscation", "Obfuscation queue is ready - %s\n", addr.ToString());
                PrepareObfuscationDenominate();
            }
        } else {
            for (CObfuscationQueue q : vecObfuscationQueue) {
                if (q.vin == dsq.vin) return;
            }

            LogPrint("obfuscation", "dsq last %d last2 %d count %d\n", pmn->nLastDsq, pmn->nLastDsq + mnodeman.size() / 5, mnodeman.nDsqCount);
            //don't allow a few nodes to dominate the queuing process
            if (pmn->nLastDsq != 0 &&
                pmn->nLastDsq + mnodeman.CountEnabled(ActiveProtocol()) / 5 > mnodeman.nDsqCount) {
                LogPrint("obfuscation", "dsq -- Masternode sending too many dsq messages. %s \n", pmn->addr.ToString());
                return;
            }
            mnodeman.nDsqCount++;
            pmn->nLastDsq = mnodeman.nDsqCount;
            pmn->allowFreeTx = true;

            LogPrint("obfuscation", "dsq - new Obfuscation queue object - %s\n", addr.ToString());
            vecObfuscationQueue.push_back(dsq);
            dsq.Relay();
            dsq.time = GetTime();
        }

    } else if (strCommand == "dsi") { //ObfuScation vIn
        int errorID;

        if (pfrom->nVersion < ActiveProtocol()) {
            LogPrintf("dsi -- incompatible version! \n");
            errorID = ERR_VERSION;
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);

            return;
        }

        if (!fMasterNode) {
            LogPrintf("dsi -- not a Masternode! \n");
            errorID = ERR_NOT_A_MN;
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);

            return;
        }

        std::vector<CTxIn> in;
        CAmount nAmount;
        CTransaction txCollateral;
        std::vector<CTxOut> out;
        vRecv >> in >> nAmount >> txCollateral >> out;

        //do we have enough users in the current session?
        if (!IsSessionReady()) {
            LogPrintf("dsi -- session not complete! \n");
            errorID = ERR_SESSION;
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
            return;
        }

        //do we have the same denominations as the current session?
        if (!IsCompatibleWithEntries(out)) {
            LogPrintf("dsi -- not compatible with existing transactions! \n");
            errorID = ERR_EXISTING_TX;
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
            return;
        }

        //check it like a transaction
        {
            CAmount nValueIn = 0;
            CAmount nValueOut = 0;
            bool missingTx = false;

            CValidationState state;
            CMutableTransaction tx;

            for (const CTxOut &o : out) {
                nValueOut += o.nValue;
                tx.vout.push_back(o);

                if (o.scriptPubKey.size() != 25) {
                    LogPrintf("dsi - non-standard pubkey detected! %s\n", o.scriptPubKey.ToString());
                    errorID = ERR_NON_STANDARD_PUBKEY;
                    pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
                    return;
                }
                if (!o.scriptPubKey.IsNormalPaymentScript()) {
                    LogPrintf("dsi - invalid script! %s\n", o.scriptPubKey.ToString());
                    errorID = ERR_INVALID_SCRIPT;
                    pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
                    return;
                }
            }

            for (const CTxIn &i : in) {
                tx.vin.push_back(i);

                LogPrint("obfuscation", "dsi -- tx in %s\n", i.ToString());

                CTransaction tx2;
                uint256 hash;
                if (GetTransaction(i.prevout.hash, tx2, hash, true)) {
                    if (tx2.vout.size() > i.prevout.n) {
                        nValueIn += tx2.vout[i.prevout.n].nValue;
                    }
                } else {
                    missingTx = true;
                }
            }

            if (nValueIn > OBFUSCATION_POOL_MAX) {
                LogPrintf("dsi -- more than Obfuscation pool max! %s\n", tx.ToString());
                errorID = ERR_MAXIMUM;
                pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
                return;
            }

            if (!missingTx) {
                if (nValueIn - nValueOut > nValueIn * .01) {
                    LogPrintf("dsi -- fees are too high! %s\n", tx.ToString());
                    errorID = ERR_FEES;
                    pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
                    return;
                }
            } else {
                LogPrintf("dsi -- missing input tx! %s\n", tx.ToString());
                errorID = ERR_MISSING_TX;
                pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
                return;
            }

            {
                LOCK(cs_main);
                if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL, false, true)) {
                    LogPrintf("dsi -- transaction not valid! \n");
                    errorID = ERR_INVALID_TX;
                    pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
                    return;
                }
            }
        }

        if (AddEntry(in, nAmount, txCollateral, out, errorID)) {
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_ACCEPTED, errorID);
            Check();

            RelayStatus(sessionID, GetState(), GetEntriesCount(), MASTERNODE_RESET);
        } else {
            pfrom->PushMessage("dssu", sessionID, GetState(), GetEntriesCount(), MASTERNODE_REJECTED, errorID);
        }

    } else if (strCommand == "dssu") { //Obfuscation status update
        if (pfrom->nVersion < ActiveProtocol()) {
            return;
        }

        if (!pSubmittedToMasternode) return;
        if ((CNetAddr)pSubmittedToMasternode->addr != (CNetAddr)pfrom->addr) {
            //LogPrintf("dssu - message doesn't match current Masternode - %s != %s\n", pSubmittedToMasternode->addr.ToString(), pfrom->addr.ToString());
            return;
        }

        int sessionIDMessage;
        int state;
        int entriesCount;
        int accepted;
        int errorID;
        vRecv >> sessionIDMessage >> state >> entriesCount >> accepted >> errorID;

        LogPrint("obfuscation", "dssu - state: %i entriesCount: %i accepted: %i error: %s \n", state, entriesCount, accepted, GetMessageByID(errorID));

        if ((accepted != 1 && accepted != 0) && sessionID != sessionIDMessage) {
            LogPrintf("dssu - message doesn't match current Obfuscation session %d %d\n", sessionID, sessionIDMessage);
            return;
        }

        StatusUpdate(state, entriesCount, accepted, errorID, sessionIDMessage);

    } else if (strCommand == "dss") { //Obfuscation Sign Final Tx

        if (pfrom->nVersion < ActiveProtocol()) {
            return;
        }

        vector<CTxIn> sigs;
        vRecv >> sigs;

        bool success = false;
        int count = 0;

        for (const CTxIn &item : sigs) {
            if (AddScriptSig(item)) success = true;
            LogPrint("obfuscation", " -- sigs count %d %d\n", (int)sigs.size(), count);
            count++;
        }

        if (success) {
            obfuScationPool.Check();
            RelayStatus(obfuScationPool.sessionID, obfuScationPool.GetState(), obfuScationPool.GetEntriesCount(), MASTERNODE_RESET);
        }
    } else if (strCommand == "dsf") { //Obfuscation Final tx
        if (pfrom->nVersion < ActiveProtocol()) {
            return;
        }

        if (!pSubmittedToMasternode) return;
        if ((CNetAddr)pSubmittedToMasternode->addr != (CNetAddr)pfrom->addr) {
            //LogPrintf("dsc - message doesn't match current Masternode - %s != %s\n", pSubmittedToMasternode->addr.ToString(), pfrom->addr.ToString());
            return;
        }

        int sessionIDMessage;
        CTransaction txNew;
        vRecv >> sessionIDMessage >> txNew;

        if (sessionID != sessionIDMessage) {
            LogPrint("obfuscation", "dsf - message doesn't match current Obfuscation session %d %d\n", sessionID, sessionIDMessage);
            return;
        }

        //check to see if input is spent already? (and probably not confirmed)
        SignFinalTransaction(txNew, pfrom);

    } else if (strCommand == "dsc") { //Obfuscation Complete

        if (pfrom->nVersion < ActiveProtocol()) {
            return;
        }

        if (!pSubmittedToMasternode) return;
        if ((CNetAddr)pSubmittedToMasternode->addr != (CNetAddr)pfrom->addr) {
            //LogPrintf("dsc - message doesn't match current Masternode - %s != %s\n", pSubmittedToMasternode->addr.ToString(), pfrom->addr.ToString());
            return;
        }

        int sessionIDMessage;
        bool error;
        int errorID;
        vRecv >> sessionIDMessage >> error >> errorID;

        if (sessionID != sessionIDMessage) {
            LogPrint("obfuscation", "dsc - message doesn't match current Obfuscation session %d %d\n", obfuScationPool.sessionID, sessionIDMessage);
            return;
        }

        obfuScationPool.CompletedTransaction(error, errorID);
    }
}

int randomizeList(int i) { return std::rand() % i; }

void CObfuscationPool::Reset()
{
    cachedLastSuccess = 0;
    lastNewBlock = 0;
    txCollateral = CMutableTransaction();
    vecMasternodesUsed.clear();
    UnlockCoins();
    SetNull();
}

void CObfuscationPool::SetNull()
{
    // MN side
    sessionUsers = 0;
    vecSessionCollateral.clear();

    // Client side
    entriesCount = 0;
    lastEntryAccepted = 0;
    countEntriesAccepted = 0;
    sessionFoundMasternode = false;

    // Both sides
    state = POOL_STATUS_IDLE;
    sessionID = 0;
    sessionDenom = 0;
    entries.clear();
    finalTransaction.vin.clear();
    finalTransaction.vout.clear();
    lastTimeChanged = GetTimeMillis();

    // -- seed random number generator (used for ordering output lists)
    unsigned int seed = 0;
    RAND_bytes((unsigned char*)&seed, sizeof(seed));
    std::srand(seed);
}

bool CObfuscationPool::SetCollateralAddress(std::string strAddress)
{
    CBitcoinAddress address;
    if (!address.SetString(strAddress)) {
        LogPrintf("CObfuscationPool::SetCollateralAddress - Invalid Obfuscation collateral address\n");
        return false;
    }
    collateralPubKey = GetScriptForDestination(address.Get());
    return true;
}

//
// Unlock coins after Obfuscation fails or succeeds
//
void CObfuscationPool::UnlockCoins()
{
    if (!pwalletMain)
        return;

    while (true) {
        TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
        if (!lockWallet) {
            MilliSleep(50);
            continue;
        }
        for (CTxIn v : lockedCoins)
            pwalletMain->UnlockCoin(v.prevout);
        break;
    }

    lockedCoins.clear();
}

std::string CObfuscationPool::GetStatus()
{
    static int showingObfuScationMessage = 0;
    showingObfuScationMessage += 10;
    std::string suffix = "";

    if (chainActive.Tip()->nHeight - cachedLastSuccess < minBlockSpacing || !masternodeSync.IsBlockchainSynced()) {
        return strAutoDenomResult;
    }
    switch (state) {
    case POOL_STATUS_IDLE:
        return _("Obfuscation is idle.");
    case POOL_STATUS_ACCEPTING_ENTRIES:
        if (entriesCount == 0) {
            showingObfuScationMessage = 0;
            return strAutoDenomResult;
        } else if (lastEntryAccepted == 1) {
            if (showingObfuScationMessage % 10 > 8) {
                lastEntryAccepted = 0;
                showingObfuScationMessage = 0;
            }
            return _("Obfuscation request complete:") + " " + _("Your transaction was accepted into the pool!");
        } else {
            std::string suffix = "";
            if (showingObfuScationMessage % 70 <= 40)
                return strprintf(_("Submitted following entries to masternode: %u / %d"), entriesCount, GetMaxPoolTransactions());
            else if (showingObfuScationMessage % 70 <= 50)
                suffix = ".";
            else if (showingObfuScationMessage % 70 <= 60)
                suffix = "..";
            else if (showingObfuScationMessage % 70 <= 70)
                suffix = "...";
            return strprintf(_("Submitted to masternode, waiting for more entries ( %u / %d ) %s"), entriesCount, GetMaxPoolTransactions(), suffix);
        }
    case POOL_STATUS_SIGNING:
        if (showingObfuScationMessage % 70 <= 40)
            return _("Found enough users, signing ...");
        else if (showingObfuScationMessage % 70 <= 50)
            suffix = ".";
        else if (showingObfuScationMessage % 70 <= 60)
            suffix = "..";
        else if (showingObfuScationMessage % 70 <= 70)
            suffix = "...";
        return strprintf(_("Found enough users, signing ( waiting %s )"), suffix);
    case POOL_STATUS_TRANSMISSION:
        return _("Transmitting final transaction.");
    case POOL_STATUS_FINALIZE_TRANSACTION:
        return _("Finalizing transaction.");
    case POOL_STATUS_ERROR:
        return _("Obfuscation request incomplete:") + " " + lastMessage + " " + _("Will retry...");
    case POOL_STATUS_SUCCESS:
        return _("Obfuscation request complete:") + " " + lastMessage;
    case POOL_STATUS_QUEUE:
        if (showingObfuScationMessage % 70 <= 30)
            suffix = ".";
        else if (showingObfuScationMessage % 70 <= 50)
            suffix = "..";
        else if (showingObfuScationMessage % 70 <= 70)
            suffix = "...";
        return strprintf(_("Submitted to masternode, waiting in queue %s"), suffix);
        ;
    default:
        return strprintf(_("Unknown state: id = %u"), state);
    }
}

//
// Check the Obfuscation progress and send client updates if a Masternode
//
void CObfuscationPool::Check()
{
    if (fMasterNode) LogPrint("obfuscation", "CObfuscationPool::Check() - entries count %lu\n", entries.size());
    //printf("CObfuscationPool::Check() %d - %d - %d\n", state, anonTx.CountEntries(), GetTimeMillis()-lastTimeChanged);

    if (fMasterNode) {
        LogPrint("obfuscation", "CObfuscationPool::Check() - entries count %lu\n", entries.size());

        // If entries is full, then move on to the next phase
        if (state == POOL_STATUS_ACCEPTING_ENTRIES && (int)entries.size() >= GetMaxPoolTransactions()) {
            LogPrint("obfuscation", "CObfuscationPool::Check() -- TRYING TRANSACTION \n");
            UpdateState(POOL_STATUS_FINALIZE_TRANSACTION);
        }
    }

    // create the finalized transaction for distribution to the clients
    if (state == POOL_STATUS_FINALIZE_TRANSACTION) {
        LogPrint("obfuscation", "CObfuscationPool::Check() -- FINALIZE TRANSACTIONS\n");
        UpdateState(POOL_STATUS_SIGNING);

        if (fMasterNode) {
            CMutableTransaction txNew;

            // make our new transaction
            for (unsigned int i = 0; i < entries.size(); i++) {
                for (const CTxOut& v : entries[i].vout)
                    txNew.vout.push_back(v);

                for (const CTxDSIn& s : entries[i].sev)
                    txNew.vin.push_back(s);
            }

            // shuffle the outputs for improved anonymity
            std::random_shuffle(txNew.vin.begin(), txNew.vin.end(), randomizeList);
            std::random_shuffle(txNew.vout.begin(), txNew.vout.end(), randomizeList);


            LogPrint("obfuscation", "Transaction 1: %s\n", txNew.ToString());
            finalTransaction = txNew;

            // request signatures from clients
            RelayFinalTransaction(sessionID, finalTransaction);
        }
    }

    // If we have all of the signatures, try to compile the transaction
    if (fMasterNode && state == POOL_STATUS_SIGNING && SignaturesComplete()) {
        LogPrint("obfuscation", "CObfuscationPool::Check() -- SIGNING\n");
        UpdateState(POOL_STATUS_TRANSMISSION);

        CheckFinalTransaction();
    }

    // reset if we're here for 10 seconds
    if ((state == POOL_STATUS_ERROR || state == POOL_STATUS_SUCCESS) && GetTimeMillis() - lastTimeChanged >= 10000) {
        LogPrint("obfuscation", "CObfuscationPool::Check() -- timeout, RESETTING\n");
        UnlockCoins();
        SetNull();
        if (fMasterNode) RelayStatus(sessionID, GetState(), GetEntriesCount(), MASTERNODE_RESET);
    }
}

void CObfuscationPool::CheckFinalTransaction()
{
    if (!fMasterNode) return; // check and relay final tx only on masternode

    CWalletTx txNew = CWalletTx(pwalletMain, finalTransaction);

    LOCK2(cs_main, pwalletMain->cs_wallet);
    {
        LogPrint("obfuscation", "Transaction 2: %s\n", txNew.ToString());

        // See if the transaction is valid
        if (!txNew.AcceptToMemoryPool(false, true, true)) {
            LogPrintf("CObfuscationPool::Check() - CommitTransaction : Error: Transaction not valid\n");
            SetNull();

            // not much we can do in this case
            UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);
            RelayCompletedTransaction(sessionID, true, ERR_INVALID_TX);
            return;
        }

        LogPrintf("CObfuscationPool::Check() -- IS MASTER -- TRANSMITTING OBFUSCATION\n");

        // sign a message

        int64_t sigTime = GetAdjustedTime();
        std::string strMessage = txNew.GetHash().ToString() + std::to_string(sigTime);
        std::string strError = "";
        std::vector<unsigned char> vchSig;
        CKey key2;
        CPubKey pubkey2;

        if (!obfuScationSigner.SetKey(strMasterNodePrivKey, strError, key2, pubkey2)) {
            LogPrintf("CObfuscationPool::Check() - ERROR: Invalid Masternodeprivkey: '%s'\n", strError);
            return;
        }

        if (!obfuScationSigner.SignMessage(strMessage, strError, vchSig, key2)) {
            LogPrintf("CObfuscationPool::Check() - Sign message failed\n");
            return;
        }

        if (!obfuScationSigner.VerifyMessage(pubkey2, vchSig, strMessage, strError)) {
            LogPrintf("CObfuscationPool::Check() - Verify message failed\n");
            return;
        }

        if (!mapObfuscationBroadcastTxes.count(txNew.GetHash())) {
            CObfuscationBroadcastTx dstx;
            dstx.tx = txNew;
            dstx.vin = activeMasternode.vin;
            dstx.vchSig = vchSig;
            dstx.sigTime = sigTime;

            mapObfuscationBroadcastTxes.insert(make_pair(txNew.GetHash(), dstx));
        }

        CInv inv(MSG_DSTX, txNew.GetHash());
        RelayInv(inv);

        // Tell the clients it was successful
        RelayCompletedTransaction(sessionID, false, MSG_SUCCESS);

        // Randomly charge clients
        ChargeRandomFees();

        // Reset
        LogPrint("obfuscation", "CObfuscationPool::Check() -- COMPLETED -- RESETTING\n");
        SetNull();
        RelayStatus(sessionID, GetState(), GetEntriesCount(), MASTERNODE_RESET);
    }
}

//
// Charge clients a fee if they're abusive
//
// Why bother? Obfuscation uses collateral to ensure abuse to the process is kept to a minimum.
// The submission and signing stages in Obfuscation are completely separate. In the cases where
// a client submits a transaction then refused to sign, there must be a cost. Otherwise they
// would be able to do this over and over again and bring the mixing to a hault.
//
// How does this work? Messages to Masternodes come in via "dsi", these require a valid collateral
// transaction for the client to be able to enter the pool. This transaction is kept by the Masternode
// until the transaction is either complete or fails.
//
void CObfuscationPool::ChargeFees()
{
    if (!fMasterNode) return;

    //we don't need to charge collateral for every offence.
    int offences = 0;
    int r = rand() % 100;
    if (r > 33) return;

    if (state == POOL_STATUS_ACCEPTING_ENTRIES) {
        for (const CTransaction& txCollateral : vecSessionCollateral) {
            bool found = false;
            for (const CObfuScationEntry& v : entries) {
                if (v.collateral == txCollateral) {
                    found = true;
                }
            }

            // This queue entry didn't send us the promised transaction
            if (!found) {
                LogPrintf("CObfuscationPool::ChargeFees -- found uncooperative node (didn't send transaction). Found offence.\n");
                offences++;
            }
        }
    }

    if (state == POOL_STATUS_SIGNING) {
        // who didn't sign?
        for (const CObfuScationEntry &v : entries) {
            for (const CTxDSIn &s : v.sev) {
                if (!s.fHasSig) {
                    LogPrintf("CObfuscationPool::ChargeFees -- found uncooperative node (didn't sign). Found offence\n");
                    offences++;
                }
            }
        }
    }

    r = rand() % 100;
    int target = 0;

    //mostly offending?
    if (offences >= Params().PoolMaxTransactions() - 1 && r > 33) return;

    //everyone is an offender? That's not right
    if (offences >= Params().PoolMaxTransactions()) return;

    //charge one of the offenders randomly
    if (offences > 1) target = 50;

    //pick random client to charge
    r = rand() % 100;

    if (state == POOL_STATUS_ACCEPTING_ENTRIES) {
        for (const CTransaction& txCollateral : vecSessionCollateral) {
            bool found = false;
            for (const CObfuScationEntry& v : entries) {
                if (v.collateral == txCollateral) {
                    found = true;
                }
            }

            // This queue entry didn't send us the promised transaction
            if (!found && r > target) {
                LogPrintf("CObfuscationPool::ChargeFees -- found uncooperative node (didn't send transaction). charging fees.\n");

                CWalletTx wtxCollateral = CWalletTx(pwalletMain, txCollateral);

                // Broadcast
                if (!wtxCollateral.AcceptToMemoryPool(true)) {
                    // This must not fail. The transaction has already been signed and recorded.
                    LogPrintf("CObfuscationPool::ChargeFees() : Error: Transaction not valid");
                }
                wtxCollateral.RelayWalletTransaction();
                return;
            }
        }
    }

    if (state == POOL_STATUS_SIGNING) {
        // who didn't sign?
        for (const CObfuScationEntry &v : entries) {
            for (const CTxDSIn &s : v.sev) {
                if (!s.fHasSig && r > target) {
                    LogPrintf("CObfuscationPool::ChargeFees -- found uncooperative node (didn't sign). charging fees.\n");

                    CWalletTx wtxCollateral = CWalletTx(pwalletMain, v.collateral);

                    // Broadcast
                    if (!wtxCollateral.AcceptToMemoryPool(false)) {
                        // This must not fail. The transaction has already been signed and recorded.
                        LogPrintf("CObfuscationPool::ChargeFees() : Error: Transaction not valid");
                    }
                    wtxCollateral.RelayWalletTransaction();
                    return;
                }
            }
        }
    }
}

// charge the collateral randomly
//  - Obfuscation is completely free, to pay miners we randomly pay the collateral of users.
void CObfuscationPool::ChargeRandomFees()
{
    if (fMasterNode) {
        int i = 0;

        for (const CTransaction& txCollateral : vecSessionCollateral) {
            int r = rand() % 100;

            /*
                Collateral Fee Charges:

                Being that Obfuscation has "no fees" we need to have some kind of cost associated
                with using it to stop abuse. Otherwise it could serve as an attack vector and
                allow endless transaction that would bloat GIANT and make it unusable. To
                stop these kinds of attacks 1 in 10 successful transactions are charged. This
                adds up to a cost of 0.001 GIC per transaction on average.
            */
            if (r <= 10) {
                LogPrintf("CObfuscationPool::ChargeRandomFees -- charging random fees. %u\n", i);

                CWalletTx wtxCollateral = CWalletTx(pwalletMain, txCollateral);

                // Broadcast
                if (!wtxCollateral.AcceptToMemoryPool(true)) {
                    // This must not fail. The transaction has already been signed and recorded.
                    LogPrintf("CObfuscationPool::ChargeRandomFees() : Error: Transaction not valid");
                }
                wtxCollateral.RelayWalletTransaction();
            }
        }
    }
}

//
// Check for various timeouts (queue objects, Obfuscation, etc)
//
void CObfuscationPool::CheckTimeout()
{
    if (!fEnableZeromint && !fMasterNode) return;

    // catching hanging sessions
    if (!fMasterNode) {
        switch (state) {
        case POOL_STATUS_TRANSMISSION:
            LogPrint("obfuscation", "CObfuscationPool::CheckTimeout() -- Session complete -- Running Check()\n");
            Check();
            break;
        case POOL_STATUS_ERROR:
            LogPrint("obfuscation", "CObfuscationPool::CheckTimeout() -- Pool error -- Running Check()\n");
            Check();
            break;
        case POOL_STATUS_SUCCESS:
            LogPrint("obfuscation", "CObfuscationPool::CheckTimeout() -- Pool success -- Running Check()\n");
            Check();
            break;
        }
    }

    // check Obfuscation queue objects for timeouts
    int c = 0;
    vector<CObfuscationQueue>::iterator it = vecObfuscationQueue.begin();
    while (it != vecObfuscationQueue.end()) {
        if ((*it).IsExpired()) {
            LogPrint("obfuscation", "CObfuscationPool::CheckTimeout() : Removing expired queue entry - %d\n", c);
            it = vecObfuscationQueue.erase(it);
        } else
            ++it;
        c++;
    }

    int addLagTime = 0;
    if (!fMasterNode) addLagTime = 10000; //if we're the client, give the server a few extra seconds before resetting.

    if (state == POOL_STATUS_ACCEPTING_ENTRIES || state == POOL_STATUS_QUEUE) {
        c = 0;

        // check for a timeout and reset if needed
        vector<CObfuScationEntry>::iterator it2 = entries.begin();
        while (it2 != entries.end()) {
            if ((*it2).IsExpired()) {
                LogPrint("obfuscation", "CObfuscationPool::CheckTimeout() : Removing expired entry - %d\n", c);
                it2 = entries.erase(it2);
                if (entries.size() == 0) {
                    UnlockCoins();
                    SetNull();
                }
                if (fMasterNode) {
                    RelayStatus(sessionID, GetState(), GetEntriesCount(), MASTERNODE_RESET);
                }
            } else
                ++it2;
            c++;
        }

        if (GetTimeMillis() - lastTimeChanged >= (OBFUSCATION_QUEUE_TIMEOUT * 1000) + addLagTime) {
            UnlockCoins();
            SetNull();
        }
    } else if (GetTimeMillis() - lastTimeChanged >= (OBFUSCATION_QUEUE_TIMEOUT * 1000) + addLagTime) {
        LogPrint("obfuscation", "CObfuscationPool::CheckTimeout() -- Session timed out (%ds) -- resetting\n", OBFUSCATION_QUEUE_TIMEOUT);
        UnlockCoins();
        SetNull();

        UpdateState(POOL_STATUS_ERROR);
        lastMessage = _("Session timed out.");
    }

    if (state == POOL_STATUS_SIGNING && GetTimeMillis() - lastTimeChanged >= (OBFUSCATION_SIGNING_TIMEOUT * 1000) + addLagTime) {
        LogPrint("obfuscation", "CObfuscationPool::CheckTimeout() -- Session timed out (%ds) -- restting\n", OBFUSCATION_SIGNING_TIMEOUT);
        ChargeFees();
        UnlockCoins();
        SetNull();

        UpdateState(POOL_STATUS_ERROR);
        lastMessage = _("Signing timed out.");
    }
}

//
// Check for complete queue
//
void CObfuscationPool::CheckForCompleteQueue()
{
    if (!fEnableZeromint && !fMasterNode) return;

    /* Check to see if we're ready for submissions from clients */
    //
    // After receiving multiple dsa messages, the queue will switch to "accepting entries"
    // which is the active state right before merging the transaction
    //
    if (state == POOL_STATUS_QUEUE && sessionUsers == GetMaxPoolTransactions()) {
        UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);

        CObfuscationQueue dsq;
        dsq.nDenom = sessionDenom;
        dsq.vin = activeMasternode.vin;
        dsq.time = GetTime();
        dsq.ready = true;
        dsq.Sign();
        dsq.Relay();
    }
}

// check to see if the signature is valid
bool CObfuscationPool::SignatureValid(const CScript& newSig, const CTxIn& newVin)
{
    CMutableTransaction txNew;
    txNew.vin.clear();
    txNew.vout.clear();

    int found = -1;
    CScript sigPubKey = CScript();
    unsigned int i = 0;

    for (CObfuScationEntry& e : entries) {
        for (const CTxOut& out : e.vout)
            txNew.vout.push_back(out);

        for (const CTxDSIn& s : e.sev) {
            txNew.vin.push_back(s);

            if (s == newVin) {
                found = i;
                sigPubKey = s.prevPubKey;
            }
            i++;
        }
    }

    if (found >= 0) { //might have to do this one input at a time?
        int n = found;
        txNew.vin[n].scriptSig = newSig;
        LogPrint("obfuscation", "CObfuscationPool::SignatureValid() - Sign with sig %s\n", newSig.ToString().substr(0, 24));
        if (!VerifyScript(txNew.vin[n].scriptSig, sigPubKey, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, MutableTransactionSignatureChecker(&txNew, n))) {
            LogPrint("obfuscation", "CObfuscationPool::SignatureValid() - Signing - Error signing input %u\n", n);
            return false;
        }
    }

    LogPrint("obfuscation", "CObfuscationPool::SignatureValid() - Signing - Successfully validated input\n");
    return true;
}

// check to make sure the collateral provided by the client is valid
bool CObfuscationPool::IsCollateralValid(const CTransaction& txCollateral)
{
    if (txCollateral.vout.size() < 1) return false;
    if (txCollateral.nLockTime != 0) return false;

    int64_t nValueIn = 0;
    int64_t nValueOut = 0;
    bool missingTx = false;

    for (const CTxOut &o : txCollateral.vout) {
        nValueOut += o.nValue;

        if (!o.scriptPubKey.IsNormalPaymentScript()) {
            LogPrintf("CObfuscationPool::IsCollateralValid - Invalid Script %s\n", txCollateral.ToString());
            return false;
        }
    }

    for (const CTxIn &i : txCollateral.vin) {
        CTransaction tx2;
        uint256 hash;
        if (GetTransaction(i.prevout.hash, tx2, hash, true)) {
            if (tx2.vout.size() > i.prevout.n) {
                nValueIn += tx2.vout[i.prevout.n].nValue;
            }
        } else {
            missingTx = true;
        }
    }

    if (missingTx) {
        LogPrint("obfuscation", "CObfuscationPool::IsCollateralValid - Unknown inputs in collateral transaction - %s\n", txCollateral.ToString());
        return false;
    }

    //collateral transactions are required to pay out OBFUSCATION_COLLATERAL as a fee to the miners
    if (nValueIn - nValueOut < OBFUSCATION_COLLATERAL) {
        LogPrint("obfuscation", "CObfuscationPool::IsCollateralValid - did not include enough fees in transaction %d\n%s\n", nValueOut - nValueIn, txCollateral.ToString());
        return false;
    }

    LogPrint("obfuscation", "CObfuscationPool::IsCollateralValid %s\n", txCollateral.ToString());

    {
        LOCK(cs_main);
        CValidationState state;
        if (!AcceptableInputs(mempool, state, txCollateral, true, NULL)) {
            if (fDebug) LogPrintf("CObfuscationPool::IsCollateralValid - didn't pass IsAcceptable\n");
            return false;
        }
    }

    return true;
}


//
// Add a clients transaction to the pool
//
bool CObfuscationPool::AddEntry(const std::vector<CTxIn>& newInput, const CAmount& nAmount, const CTransaction& txCollateral, const std::vector<CTxOut>& newOutput, int& errorID)
{
    if (!fMasterNode) return false;

    for (CTxIn in : newInput) {
        if (in.prevout.IsNull() || nAmount < 0) {
            LogPrint("obfuscation", "CObfuscationPool::AddEntry - input not valid!\n");
            errorID = ERR_INVALID_INPUT;
            sessionUsers--;
            return false;
        }
    }

    if (!IsCollateralValid(txCollateral)) {
        LogPrint("obfuscation", "CObfuscationPool::AddEntry - collateral not valid!\n");
        errorID = ERR_INVALID_COLLATERAL;
        sessionUsers--;
        return false;
    }

    if ((int)entries.size() >= GetMaxPoolTransactions()) {
        LogPrint("obfuscation", "CObfuscationPool::AddEntry - entries is full!\n");
        errorID = ERR_ENTRIES_FULL;
        sessionUsers--;
        return false;
    }

    for (CTxIn in : newInput) {
        LogPrint("obfuscation", "looking for vin -- %s\n", in.ToString());
        for (const CObfuScationEntry& v : entries) {
            for (const CTxDSIn& s : v.sev) {
                if ((CTxIn)s == in) {
                    LogPrint("obfuscation", "CObfuscationPool::AddEntry - found in vin\n");
                    errorID = ERR_ALREADY_HAVE;
                    sessionUsers--;
                    return false;
                }
            }
        }
    }

    CObfuScationEntry v;
    v.Add(newInput, nAmount, txCollateral, newOutput);
    entries.push_back(v);

    LogPrint("obfuscation", "CObfuscationPool::AddEntry -- adding %s\n", newInput[0].ToString());
    errorID = MSG_ENTRIES_ADDED;

    return true;
}

bool CObfuscationPool::AddScriptSig(const CTxIn& newVin)
{
    LogPrint("obfuscation", "CObfuscationPool::AddScriptSig -- new sig  %s\n", newVin.scriptSig.ToString().substr(0, 24));


    for (const CObfuScationEntry& v : entries) {
        for (const CTxDSIn& s : v.sev) {
            if (s.scriptSig == newVin.scriptSig) {
                LogPrint("obfuscation", "CObfuscationPool::AddScriptSig - already exists\n");
                return false;
            }
        }
    }

    if (!SignatureValid(newVin.scriptSig, newVin)) {
        LogPrint("obfuscation", "CObfuscationPool::AddScriptSig - Invalid Sig\n");
        return false;
    }

    LogPrint("obfuscation", "CObfuscationPool::AddScriptSig -- sig %s\n", newVin.ToString());

    for (CTxIn& vin : finalTransaction.vin) {
        if (newVin.prevout == vin.prevout && vin.nSequence == newVin.nSequence) {
            vin.scriptSig = newVin.scriptSig;
            vin.prevPubKey = newVin.prevPubKey;
            LogPrint("obfuscation", "CObfuScationPool::AddScriptSig -- adding to finalTransaction  %s\n", newVin.scriptSig.ToString().substr(0, 24));
        }
    }
    for (unsigned int i = 0; i < entries.size(); i++) {
        if (entries[i].AddSig(newVin)) {
            LogPrint("obfuscation", "CObfuScationPool::AddScriptSig -- adding  %s\n", newVin.scriptSig.ToString().substr(0, 24));
            return true;
        }
    }

    LogPrintf("CObfuscationPool::AddScriptSig -- Couldn't set sig!\n");
    return false;
}

// Check to make sure everything is signed
bool CObfuscationPool::SignaturesComplete()
{
    for (const CObfuScationEntry& v : entries) {
        for (const CTxDSIn& s : v.sev) {
            if (!s.fHasSig) return false;
        }
    }
    return true;
}

//
// Execute a Obfuscation denomination via a Masternode.
// This is only ran from clients
//
void CObfuscationPool::SendObfuscationDenominate(std::vector<CTxIn>& vin, std::vector<CTxOut>& vout, CAmount amount)
{
    if (fMasterNode) {
        LogPrintf("CObfuscationPool::SendObfuscationDenominate() - Obfuscation from a Masternode is not supported currently.\n");
        return;
    }

    if (txCollateral == CMutableTransaction()) {
        LogPrintf("CObfuscationPool:SendObfuscationDenominate() - Obfuscation collateral not set");
        return;
    }

    // lock the funds we're going to use
    for (CTxIn in : txCollateral.vin)
        lockedCoins.push_back(in);

    for (CTxIn in : vin)
        lockedCoins.push_back(in);

    //for (CTxOut o : vout)
    //    LogPrintf(" vout - %s\n", o.ToString());


    // we should already be connected to a Masternode
    if (!sessionFoundMasternode) {
        LogPrintf("CObfuscationPool::SendObfuscationDenominate() - No Masternode has been selected yet.\n");
        UnlockCoins();
        SetNull();
        return;
    }

    if (!CheckDiskSpace()) {
        UnlockCoins();
        SetNull();
        fEnableZeromint = false;
        LogPrintf("CObfuscationPool::SendObfuscationDenominate() - Not enough disk space, disabling Obfuscation.\n");
        return;
    }

    UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);

    LogPrintf("CObfuscationPool::SendObfuscationDenominate() - Added transaction to pool.\n");

    ClearLastMessage();

    //check it against the memory pool to make sure it's valid
    {
        CAmount nValueOut = 0;

        CValidationState state;
        CMutableTransaction tx;

        for (const CTxOut& o : vout) {
            nValueOut += o.nValue;
            tx.vout.push_back(o);
        }

        for (const CTxIn& i : vin) {
            tx.vin.push_back(i);

            LogPrint("obfuscation", "dsi -- tx in %s\n", i.ToString());
        }

        LogPrintf("Submitting tx %s\n", tx.ToString());

        while (true) {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) {
                MilliSleep(50);
                continue;
            }
            if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL, false, true)) {
                LogPrintf("dsi -- transaction not valid! %s \n", tx.ToString());
                UnlockCoins();
                SetNull();
                return;
            }
            break;
        }
    }

    // store our entry for later use
    CObfuScationEntry e;
    e.Add(vin, amount, txCollateral, vout);
    entries.push_back(e);

    RelayIn(entries[0].sev, entries[0].amount, txCollateral, entries[0].vout);
    Check();
}

// Incoming message from Masternode updating the progress of Obfuscation
//    newAccepted:  -1 mean's it'n not a "transaction accepted/not accepted" message, just a standard update
//                  0 means transaction was not accepted
//                  1 means transaction was accepted

bool CObfuscationPool::StatusUpdate(int newState, int newEntriesCount, int newAccepted, int& errorID, int newSessionID)
{
    if (fMasterNode) return false;
    if (state == POOL_STATUS_ERROR || state == POOL_STATUS_SUCCESS) return false;

    UpdateState(newState);
    entriesCount = newEntriesCount;

    if (errorID != MSG