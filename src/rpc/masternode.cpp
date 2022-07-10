// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
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

#include <boost/tokenizer.hpp>
#include <fstream>

namespace {

    void ltrim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
            return !std::isspace(ch);
        }));
    }

}

UniValue getpoolinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpoolinfo\n"
            "\nReturns anonymous pool-related information\n"

            "\nResult:\n"
            "{\n"
            "  \"current\": \"addr\",    (string) GIANT address of current masternode\n"
            "  \"state\": xxxx,        (string) unknown\n"
            "  \"entries\": xxxx,      (numeric) Number of entries\n"
            "  \"accepted\": xxxx,     (numeric) Number of entries accepted\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getpoolinfo", "") + HelpExampleRpc("getpoolinfo", ""));

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("current_masternode", mnodeman.GetCurrentMasterNode(CMasternode::Level::UNKNOWN)->addr.ToString()));
    obj.push_back(Pair("state", obfuScationPool.GetState()));
    obj.push_back(Pair("entries", obfuScationPool.GetEntriesCount()));
    obj.push_back(Pair("entries_accepted", obfuScationPool.GetCountEntriesAccepted()));
    return obj;
}

UniValue listmasternodes(const UniValue& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw runtime_error(
            "listmasternodes ( \"filter\" )\n"
            "\nGet a ranked list of masternodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"level\": n,          (numeric) Masternode Level\n"
            "    \"rank\": n,           (numeric) Masternode Rank (or 0 if not enabled)\n"
            "    \"txhash\": \"hash\",    (string) Collateral transaction hash\n"
            "    \"outidx\": n,         (numeric) Collateral transaction output index\n"
            "    \"pubkey\": \"key\",   (string) Masternode public key used for message broadcasting\n"
            "    \"status\": s,         (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"addr\",      (string) Masternode GIANT address\n"
            "    \"version\": v,        (numeric) Masternode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode has been active\n"
            "    \"lastpaid\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode was last paid\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listmasternodes", "") + HelpExampleRpc("listmasternodes", ""));

    UniValue ret(UniValue::VARR);
    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
        nHeight = pindex->nHeight;
    }
    std::vector<pair<int, CMasternode> > vMasternodeRanks = mnodeman.GetMasternodeRanks(nHeight);
    for (PAIRTYPE(int, CMasternode) & s : vMasternodeRanks) {
        UniValue obj(UniValue::VOBJ);
        std::string strVin = s.second.vin.prevout.ToStringShort();
        std::string strTxHash = s.second.vin.prevout.hash.ToString();
        uint32_t oIdx = s.second.vin.prevout.n;

        CMasternode* mn = mnodeman.Find(s.second.vin);

        if (mn != NULL) {
            if (strFilter != "" && strTxHash.find(strFilter) == string::npos &&
                mn->Status().find(strFilter) == string::npos &&
                CBitcoinAddress(mn->pubKeyCollateralAddress.GetID()).ToString().find(strFilter) == string::npos) continue;

            std::string strStatus = mn->Status();
            std::string strHost;
            int port;
            SplitHostPort(mn->addr.ToString(), port, strHost);
            CNetAddr node = CNetAddr(strHost, false);
            std::string strNetwork = GetNetworkName(node.GetNetwork());

            obj.push_back(Pair("level", mn->GetLevelText()));
            obj.push_back(Pair("rank", (strStatus == "ENABLED" ? s.first : 0)));
            obj.push_back(Pair("network", strNetwork));
            obj.push_back(Pair("txhash", strTxHash));
            obj.push_back(Pair("outidx", (uint64_t)oIdx));
            obj.push_back(Pair("pubkey", HexStr(mn->pubKeyMasternode)));
            obj.push_back(Pair("status", strStatus));
            obj.push_back(Pair("addr", CBitcoinAddress(mn->pubKeyCollateralAddress.GetID()).ToString()));
            obj.push_back(Pair("version", mn->protocolVersion));
            obj.push_back(Pair("lastseen", (int64_t)mn->lastPing.sigTime));
            obj.push_back(Pair("activetime", (int64_t)(mn->lastPing.sigTime - mn->sigTime)));
            obj.push_back(Pair("lastpaid", (int64_t)mn->GetLastPaid()));

            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue masternodeconnect(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1))
        throw runtime_error(
            "masternodeconnect \"address\"\n"
            "\nAttempts to connect to specified masternode address\n"

            "\nArguments:\n"
            "1. \"address\"     (string, required) IP or net address to connect to\n"

            "\nExamples:\n" +
            HelpExampleCli("masternodeconnect", "\"192.168.0.6:40444\"") + HelpExampleRpc("masternodeconnect", "\"192.168.0.6:40444\""));

    std::string strAddress = params[0].get_str();

    CService addr = CService(strAddress);

    CNode* pnode = ConnectNode((CAddress)addr, NULL, false);
    if (pnode) {
        pnode->Release();
        return NullUniValue;
    } else {
        throw runtime_error("error connecting\n");
    }
}

UniValue getmasternodecount (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() > 0))
        throw runtime_error(
            "getmasternodecount\n"
            "\nGet masternode count values\n"

            "\nResult:\n"
            "{\n"
            "  \"all\": n, (numeric) Total masternodes"
            "  \"total\": [\n"
            "     {\n"
            "       \"level\": n, (numeric) Masternodes level\n"
            "       \"count\": n  (numeric) Total count\n"
            "     }\n"
            "     ,...\n"
            "   ],\n"
            "  \"enabled\": [\n"
            "     {\n"
            "       \"level\": n, (numeric) Masternodes level\n"
            "       \"count\": n  (numeric) Enabled masternodes\n"
            "     }\n"
            "     ,...\n"
            "   ],\n"
            "  \"obfcompat\": [\n"
            "     {\n"
            "       \"level\": n, (numeric) Masternodes level\n"
            "       \"count\": n  (numeric) Obfuscation Compatible\n"
            "     }\n"
            "     ,...\n"
            "   ],\n"
            "  \"stable\": [\n"
            "     {\n"
            "       \"level\": n, (numeric) Masternodes level\n"
            "       \"count\": n  (numeric) Stable count\n"
            "     }\n"
            "     ,...\n"
            "   ],\n"
            "  \"inqueue\": [\n"
            "     {\n"
            "       \"level\": n, (numeric) Masternodes level\n"
            "       \"count\": n  (numeric) Masternodes in queue\n"
            "     }\n"
            "     ,...\n"
            "   ],\n"
            "  \"ipv4\": [\n"
            "     {\n"
            "       \"level\": n, (numeric) Masternodes level\n"
            "       \"count\": n  (numeric) Masternodes ipv4\n"
            "     }\n"
            "     ,...\n"
            "   ],\n"
            "  \"ipv6\": [\n"
            "     {\n"
            "       \"level\": n, (numeric) Masternodes level\n"
            "       \"count\": n  (numeric) Masternodes ipv6\n"
            "     }\n"
            "     ,...\n"
            "   ],\n"
            "  \"onion\": [\n"
            "     {\n"
            "       \"level\": n, (numeric) Masternodes level\n"
            "       \"count\": n  (numeric) Masternodes onion\n"
            "     }\n"
            "     ,...\n"
            "   ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodecount", "") + HelpExampleRpc("getmasternodecount", ""));


    UniValue total{UniValue::VARR};
    UniValue stable{UniValue::VARR};
    UniValue obfcompat{UniValue::VARR};
    UniValue enabled{UniValue::VARR};
    UniValue inqueue{UniValue::VARR};
    UniValue ipv4{UniValue::VARR};
    UniValue ipv6{UniValue::VARR};
    UniValue onion{UniValue::VARR};

    auto chainTip = chainActive.Tip();

    for(int l = CMasternode::begin(chainActive.Height()); l <= CMasternode::end(chainActive.Height()); l++) {

        std::string levelText = CMasternode::GetLevelText(l);

        UniValue totalByLevel{UniValue::VOBJ};
        totalByLevel.push_back(Pair("level", levelText));
        totalByLevel.push_back(Pair("count", mnodeman.size(l)));
        total.push_back(totalByLevel);

        UniValue stableByLevel{UniValue::VOBJ};
        stableByLevel.push_back(Pair("level", levelText));
        stableByLevel.push_back(Pair("count", mnodeman.stable_size(l)));
        stable.push_back(stableByLevel);

        UniValue enabledByLevel{UniValue::VOBJ};
        enabledByLevel.push_back(Pair("level", levelText));
        enabledByLevel.push_back(Pair("count", mnodeman.CountEnabled(l)));
        enabled.push_back(enabledByLevel);

        UniValue inqueueByLevel{UniValue::VOBJ};
        int inqueueCount = 0u;
        if(chainTip) {
            mnodeman.GetNextMasternodeInQueueForPayment(chainTip->nHeight, l, true, inqueueCount);
        }
        inqueueByLevel.push_back(Pair("level", levelText));
        inqueueByLevel.push_back(Pair("count", inqueueCount));

        inqueue.push_back(inqueueByLevel);

        UniValue obfcomatByLevel{UniValue::VOBJ};
        obfcomatByLevel.push_back(Pair("level", levelText));
        obfcomatByLevel.push_back(Pair("count", mnodeman.CountEnabled(l, ActiveProtocol())));
        obfcompat.push_back(obfcomatByLevel);

        int ipv4Count = 0;
        int ipv6Count = 0;
        int onionCount = 0;
        mnodeman.CountNetworks(ActiveProtocol(), ipv4Count, ipv6Count, onionCount, l);

        UniValue ipv4ByLevel{UniValue::VOBJ};
        ipv4ByLevel.push_back(Pair("level", levelText));
        ipv4ByLevel.push_back(Pair("count", ipv4Count));
        ipv4.push_back(ipv4ByLevel);

        UniValue ipv6ByLevel{UniValue::VOBJ};
        ipv6ByLevel.push_back(Pair("level", levelText));
        ipv6ByLevel.push_back(Pair("count", ipv6Count));
        ipv6.push_back(ipv6ByLevel);

        UniValue onionByLevel{UniValue::VOBJ};
        onionByLevel.push_back(Pair("level", levelText));
        onionByLevel.push_back(Pair("count", onionCount));
        onion.push_back(onionByLevel);
    }

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("all", mnodeman.size()));
    obj.push_back(Pair("total", total));
    obj.push_back(Pair("enabled", enabled));
    obj.push_back(Pair("obfcompat", obfcompat));
    obj.push_back(Pair("stable", stable));
    obj.push_back(Pair("inqueue", inqueue));
    obj.push_back(Pair("ipv4", ipv4));
    obj.push_back(Pair("ipv6", ipv6));
    obj.push_back(Pair("onion", onion));
    return obj;
}

UniValue masternodecurrent (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "masternodecurrent\n"
            "\nGet current masternode winner\n"

            "\nResult:\n"
            "{\n"
            "  \"level\": xxxx,         (numeric) MN level\n"
            "  \"protocol\": xxxx,      (numeric) Protocol version\n"
            "  \"txhash\": \"xxxx\",    (string) Collateral transaction hash\n"
            "  \"pubkey\": \"xxxx\",    (string) MN Public key\n"
            "  \"lastseen\": xxx,       (numeric) Time since epoch of last seen\n"
            "  \"activeseconds\": xxx,  (numeric) Seconds MN has been active\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("masternodecurrent", "") + HelpExampleRpc("masternodecurrent", ""));

    UniValue result{UniValue::VARR};
    for(int l = CMasternode::begin(chainActive.Height()); l <= CMasternode::end(chainActive.Height()); l++) {
        CMasternode* winner = mnodeman.GetCurrentMasterNode(l, 1);
        if(!winner)
            continue;

        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("level", winner->GetLevelText()));
        obj.push_back(Pair("protocol", (int64_t)winner->protocolVersion));
        obj.push_back(Pair("txhash", winner->vin.prevout.hash.ToString()));
        obj.push_back(Pair("outputidx", (uint64_t)winner->vin.prevout.n));
        obj.push_back(Pair("pubkey", CBitcoinAddress(winner->pubKeyCollateralAddress.GetID()).ToString()));
        obj.push_back(Pair("lastseen", (winner->lastPing == CMasternodePing()) ? winner->sigTime : (int64_t)winner->lastPing.sigTime));
        obj.push_back(Pair("activeseconds", (winner->lastPing == CMasternodePing()) ? 0 : (int64_t)(winner->lastPing.sigTime - winner->sigTime)));
        result.push_back(obj);
    }
    return result;
}

UniValue masternodedebug (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "masternodedebug\n"
            "\nPrint masternode status\n"

            "\nResult:\n"
            "\"status\"     (string) Masternode status message\n"

            "\nExamples:\n" +
            HelpExampleCli("masternodedebug", "") + HelpExampleRpc("masternodedebug", ""));

    if (activeMasternode.status != ACTIVE_MASTERNODE_INITIAL || !masternodeSync.IsSynced())
        return activeMasternode.GetStatus();

    CTxIn vin = CTxIn();
    CPubKey pubkey;
    CKey key;
    if (!activeMasternode.GetMasterNodeVin(vin, pubkey, key))
        throw runtime_error("Missing masternode input, please look at the documentation for instructions on masternode creation\n");
    else
        return activeMasternode.GetStatus();
}

UniValue startmasternode (const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();

        // Backwards compatibility with legacy 'masternode' super-command forwarder
        if (strCommand == "start") strCommand = "local";
        if (strCommand == "start-alias") strCommand = "alias";
        if (strCommand == "start-all") strCommand = "all";
        if (strCommand == "start-many") strCommand = "many";
        if (strCommand == "start-missing") strCommand = "missing";
        if (strCommand == "start-disabled") strCommand = "disabled";
    }

    if (fHelp || params.size() < 2 || params.size() > 3 ||
        (params.size() == 2 && (strCommand != "local" && strCommand != "all" && strCommand != "many" && strCommand != "missing" && strCommand != "disabled")) ||
        (params.size() == 3 && strCommand != "alias"))
        throw runtime_error(
            "startmasternode \"local|all|many|missing|disabled|alias\" lockwallet ( \"alias\" )\n"
            "\nAttempts to start one or more masternode(s)\n"

            "\nArguments:\n"
            "1. set         (string, required) Specify which set of masternode(s) to start.\n"
            "2. lockwallet  (boolean, required) Lock wallet after completion.\n"
            "3. alias       (string) Masternode alias. Required if using 'alias' as the set.\n"

            "\nResult: (for 'local' set):\n"
            "\"status\"     (string) Masternode status message\n"

            "\nResult: (for other sets):\n"
            "{\n"
            "  \"overall\": \"xxxx\",     (string) Overall status message\n"
            "  \"detail\": [\n"
            "    {\n"
            "      \"node\": \"xxxx\",    (string) Node name or alias\n"
            "      \"result\": \"xxxx\",  (string) 'success' or 'failed'\n"
            "      \"error\": \"xxxx\"    (string) Error message, if failed\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("startmasternode", "\"alias\" \"0\" \"my_mn\"") + HelpExampleRpc("startmasternode", "\"alias\" \"0\" \"my_mn\""));

    bool fLock = (params[1].get_str() == "true" ? true : false);

    EnsureWalletIsUnlocked();

    if (strCommand == "local") {
        if (!fMasterNode) throw runtime_error("you must set masternode=1 in the configuration\n");

        if (activeMasternode.status != ACTIVE_MASTERNODE_STARTED) {
            activeMasternode.status = ACTIVE_MASTERNODE_INITIAL; // TODO: consider better way
            activeMasternode.ManageStatus();
            if (fLock)
                pwalletMain->Lock();
        }

        return activeMasternode.GetStatus();
    }

    if (strCommand == "all" || strCommand == "many" || strCommand == "missing" || strCommand == "disabled") {
        if ((strCommand == "missing" || strCommand == "disabled") &&
            (masternodeSync.RequestedMasternodeAssets <= MASTERNODE_SYNC_LIST ||
                masternodeSync.RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED)) {
            throw runtime_error("You can't use this command until masternode list is synced\n");
        }

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
            std::string errorMessage;
            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;
            CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(nIndex));
            CMasternode* pmn = mnodeman.Find(vin);
            CMasternodeBroadcast mnb;

            if (pmn != NULL) {
                if (strCommand == "missing") continue;
                if (strCommand == "disabled" && pmn->IsEnabled()) continue;
            }

            bool result = activeMasternode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, mnb);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", result ? "success