// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/giant-config.h"
#endif

#include "init.h"

#include "zgic/accumulators.h"
#include "activemasternode.h"
#include "addrman.h"
#include "amount.h"
#include "checkpoints.h"
#include "compat/sanity.h"
#include "httpserver.h"
#include "httprpc.h"
#include "invalid.h"
#include "key.h"
#include "main.h"
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "miner.h"
#include "net.h"
#include "rpc/server.h"
#include "script/standard.h"
#include "scheduler.h"
#include "spork.h"
#include "sporkdb.h"
#include "txdb.h"
#include "torcontrol.h"
#include "guiinterface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#include "zgic/accumulatorcheckpoints.h"
#include "zgicchain.h"

#ifdef ENABLE_WALLET
#include "wallet/db.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"

#endif

#include <fstream>
#include <stdint.h>
#include <stdio.h>

#ifndef WIN32
#include <signal.h>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <openssl/crypto.h>

#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

using namespace boost;
using namespace std;

#ifdef ENABLE_WALLET
CWallet* pwalletMain = NULL;
CzGICWallet* zwalletMain = NULL;
int nWalletBackups = 10;
#endif
volatile bool fFeeEstimatesInitialized = false;
volatile bool fRestartRequested = false; // true: restart false: shutdown
extern std::list<uint256> listAccCheckpointsNoDB;

#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = NULL;
#endif

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files, don't count towards to fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

/** Used to pass flags to the Bind() function */
enum BindFlags {
    BF_NONE = 0,
    BF_EXPLICIT = (1U << 0),
    BF_REPORT_ERROR = (1U << 1),
    BF_WHITELIST = (1U << 2),
};

static const char* FEE_ESTIMATES_FILENAME = "fee_estimates.dat";
CClientUIInterface uiInterface;

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets fRequestShutdown, which triggers
// the DetectShutdownThread(), which interrupts the main thread group.
// DetectShutdownThread() then exits, which causes AppInit() to
// continue (it .joins the shutdown thread).
// Shutdown() is then
// called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Note that if running -daemon the parent process returns from AppInit2
// before adding any threads to the threadGroup, so .join_all() returns
// immediately and the parent exits from main().
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// fRequestShutdown getting set, and then does the normal Qt
// shutdown thing.
//

volatile bool fRequestShutdown = false;

void StartShutdown() {
    fRequestShutdown = true;
}

bool ShutdownRequested() {
    return fRequestShutdown || fRestartRequested;
}

class CCoinsViewErrorCatcher : public CCoinsViewBacked {
public:

    CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {
    }

    bool GetCoins(const uint256& txid, CCoins& coins) const {
        try {
            return CCoinsViewBacked::GetCoins(txid, coins);
        } catch (const std::runtime_error& e) {
            uiInterface.ThreadSafeMessageBox(_("Error reading from database, shutting down."), "", CClientUIInterface::MSG_ERROR);
            LogPrintf("Error reading from database: %s\n", e.what());
            // Starting the shutdown sequence and returning false to the caller would be
            // interpreted as 'entry not found' (as opposed to unable to read data), and
            // could lead to invalid interpration. Just exit immediately, as we can't
            // continue anyway, and all writes should be atomic.
            abort();
        }
    }
    // Writes do not need similar protection, as failure to write is handled by the caller.
};

static CCoinsViewDB* pcoinsdbview = NULL;
static CCoinsViewErrorCatcher* pcoinscatcher = NULL;
static boost::scoped_ptr<ECCVerifyHandle> globalVerifyHandle;

static boost::thread_group threadGroup;
static CScheduler scheduler;

void Interrupt() {
    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();
    InterruptTorControl();
}

/** Preparing steps before shutting down or restarting the wallet */
void PrepareShutdown() {
    fRequestShutdown = true; // Needed when we shutdown the wallet
    fRestartRequested = true; // Needed when we restart the wallet
    LogPrintf("%s: In progress...\n", __func__);
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown)
        return;

    /// Note: Shutdown() must be able to handle cases in which AppInit2() failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    RenameThread("giant-shutoff");
    mempool.AddTransactionsUpdated(1);
    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();
#ifdef ENABLE_WALLET
    if (pwalletMain)
        bitdb.Flush(false);
    GenerateBitcoins(false, NULL, 0);
#endif
    StopNode();
    DumpMasternodes();
    DumpBudgets();
    DumpMasternodePayments();
    UnregisterNodeSignals(GetNodeSignals());

    // After everything has been shut down, but before things get flushed, stop the
    // CScheduler/checkqueue threadGroup
    threadGroup.interrupt_all();
    threadGroup.join_all();

    if (fFeeEstimatesInitialized) {
        boost::filesystem::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        CAutoFile est_fileout(fopen(est_path.string().c_str(), "wb"), SER_DISK, CLIENT_VERSION);
        if (!est_fileout.IsNull())
            mempool.WriteFeeEstimates(est_fileout);
        else
            LogPrintf("%s: Failed to write fee estimates to %s\n", __func__, est_path.string());
        fFeeEstimatesInitialized = false;
    }

    {
        LOCK(cs_main);
        if (pcoinsTip != NULL) {
            FlushStateToDisk();

            //record that client took the proper shutdown procedure
            pblocktree->WriteFlag("shutdown", true);
        }
        delete pcoinsTip;
        pcoinsTip = NULL;
        delete pcoinscatcher;
        pcoinscatcher = NULL;
        delete pcoinsdbview;
        pcoinsdbview = NULL;
        delete pblocktree;
        pblocktree = NULL;
        delete zerocoinDB;
        zerocoinDB = NULL;
        delete pSporkDB;
        pSporkDB = NULL;
    }
#ifdef ENABLE_WALLET
    if (pwalletMain)
        bitdb.Flush(true);
#endif

#if ENABLE_ZMQ
    if (pzmqNotificationInterface) {
        UnregisterValidationInterface(pzmqNotificationInterface);
        delete pzmqNotificationInterface;
        pzmqNotificationInterface = NULL;
    }
#endif

#ifndef WIN32
    try {
        boost::filesystem::remove(GetPidFile());
    } catch (const boost::filesystem::filesystem_error& e) {
        LogPrintf("%s: Unable to remove pidfile: %s\n", __func__, e.what());
    }
#endif
    UnregisterAllValidationInterfaces();
}

/**
 * Shutdown is split into 2 parts:
 * Part 1: shut down everything but the main wallet instance (done in PrepareShutdown() )
 * Part 2: delete wallet instance
 *
 * In case of a restart PrepareShutdown() was already called before, but this method here gets
 * called implicitly when the parent object is deleted. In this case we have to skip the
 * PrepareShutdown() part because it was already executed and just delete the wallet instance.
 */
void Shutdown() {
    // Shutdown part 1: prepare shutdown
    if (!fRestartRequested) {
        PrepareShutdown();
    }
    // Shutdown part 2: Stop TOR thread and delete wallet instance
    StopTorControl();
    // Shutdown witness thread if it's enabled
    if (nLocalServices == NODE_BLOOM_LIGHT_ZC) {
        lightWorker.StopLightZgicThread();
    }
#ifdef ENABLE_WALLET
    delete pwalletMain;
    pwalletMain = NULL;
    delete zwalletMain;
    zwalletMain = NULL;
#endif
    globalVerifyHandle.reset();
    ECC_Stop();
    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do, so:
 */
void HandleSIGTERM(int) {
    fRequestShutdown = true;
}

void HandleSIGHUP(int) {
    fReopenDebugLog = true;
}

bool static InitError(const std::string& str) {
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

bool static InitWarning(const std::string& str) {
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
    return true;
}

bool static Bind(const CService& addr, unsigned int flags) {
    if (!(flags & BF_EXPLICIT) && IsLimited(addr))
        return false;
    std::string strError;
    if (!BindListenPort(addr, strError, (flags & BF_WHITELIST) != 0)) {
        if (flags & BF_REPORT_ERROR)
            return InitError(strError);
        return false;
    }
    return true;
}

void OnRPCStarted() {
    uiInterface.NotifyBlockTip.connect(RPCNotifyBlockChange);
}

void OnRPCStopped() {
    uiInterface.NotifyBlockTip.disconnect(RPCNotifyBlockChange);
    //RPCNotifyBlockChange(0);
    cvBlockChange.notify_all();
    LogPrint("rpc", "RPC stopped.\n");
}

void OnRPCPreCommand(const CRPCCommand& cmd) {
#ifdef ENABLE_WALLET
    if (cmd.reqWallet && !pwalletMain)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");
#endif

    // Observe safe mode
    string strWarning = GetWarnings("rpc");
    if (strWarning != "" && !GetBoolArg("-disablesafemode", false) &&
            !cmd.okSafeMode)
        throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, string("Safe mode: ") + strWarning);
}

std::string HelpMessage(HelpMessageMode mode) {

    // When adding new options to the categories, please keep and ensure alphabetical ordering.
    string strUsage = HelpMessageGroup(_("Options:"));
    strUsage += HelpMessageOpt("-?", _("This help message"));
    strUsage += HelpMessageOpt("-version", _("Print version and exit"));
    strUsage += HelpMessageOpt("-alertnotify=<cmd>", _("Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)"));
    strUsage += HelpMessageOpt("-alerts", strprintf(_("Receive and display P2P network alerts (default: %u)"), DEFAULT_ALERTS));
    strUsage += HelpMessageOpt("-blocknotify=<cmd>", _("Execute command when the best block changes (%s in cmd is replaced by block hash)"));
    strUsage += HelpMessageOpt("-blocksizenotify=<cmd>", _("Execute command when the best block changes and its size is over (%s in cmd is replaced by block hash, %d with the block size)"));
    strUsage += HelpMessageOpt("-checkblocks=<n>", strprintf(_("How many blocks to check at startup (default: %u, 0 = all)"), 500));
    strUsage += HelpMessageOpt("-conf=<file>", strprintf(_("Specify configuration file (default: %s)"), "giant.conf"));
    if (mode == HMM_BITCOIND) {
#if !defined(WIN32)
        strUsage += HelpMessageOpt("-daemon", _("Run in the background as a daemon and accept commands"));
#endif
    }
    strUsage += HelpMessageOpt("-datadir=<dir>", _("Specify data directory"));
    strUsage += HelpMessageOpt("-dbcache=<n>", strprintf(_("Set database cache size in megabytes (%d to %d, default: %d)"), nMinDbCache, nMaxDbCache, nDefaultDbCache));
    strUsage += HelpMessageOpt("-loadblock=<file>", _("Imports blocks from external blk000??.dat file") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-maxreorg=<n>", strprintf(_("Set the Maximum reorg depth (default: %u)"), Params(CBaseChainParams::MAIN).MaxReorganizationDepth()));
    strUsage += HelpMessageOpt("-maxorphantx=<n>", strprintf(_("Keep at most <n> unconnectable transactions in memory (default: %u)"), DEFAUL