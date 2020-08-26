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
    strUsage += HelpMessageOpt("-maxorphantx=<n>", strprintf(_("Keep at most <n> unconnectable transactions in memory (default: %u)"), DEFAULT_MAX_ORPHAN_TRANSACTIONS));
    strUsage += HelpMessageOpt("-par=<n>", strprintf(_("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)"), -(int) boost::thread::hardware_concurrency(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS));
#ifndef WIN32
    strUsage += HelpMessageOpt("-pid=<file>", strprintf(_("Specify pid file (default: %s)"), "giantd.pid"));
#endif
    strUsage += HelpMessageOpt("-reindex", _("Rebuild block chain index from current blk000??.dat files") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-reindexaccumulators", _("Reindex the accumulator database") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-reindexmoneysupply", _("Reindex the GIC and zGIC money supply statistics") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-resync", _("Delete blockchain folders and resync from scratch") + " " + _("on startup"));
#if !defined(WIN32)
    strUsage += HelpMessageOpt("-sysperms", _("Create new files with system default permissions, instead of umask 077 (only effective with disabled wallet functionality)"));
#endif
    strUsage += HelpMessageOpt("-txindex", strprintf(_("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)"), 0));
    strUsage += HelpMessageOpt("-forcestart", _("Attempt to force blockchain corruption recovery") + " " + _("on startup"));

    strUsage += HelpMessageGroup(_("Connection options:"));
    strUsage += HelpMessageOpt("-addnode=<ip>", _("Add a node to connect to and attempt to keep the connection open"));
    strUsage += HelpMessageOpt("-banscore=<n>", strprintf(_("Threshold for disconnecting misbehaving peers (default: %u)"), 100));
    strUsage += HelpMessageOpt("-bantime=<n>", strprintf(_("Number of seconds to keep misbehaving peers from reconnecting (default: %u)"), 86400));
    strUsage += HelpMessageOpt("-bind=<addr>", _("Bind to given address and always listen on it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-connect=<ip>", _("Connect only to the specified node(s)"));
    strUsage += HelpMessageOpt("-discover", _("Discover own IP address (default: 1 when listening and no -externalip)"));
    strUsage += HelpMessageOpt("-dns", _("Allow DNS lookups for -addnode, -seednode and -connect") + " " + _("(default: 1)"));
    strUsage += HelpMessageOpt("-dnsseed", _("Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect)"));
    strUsage += HelpMessageOpt("-externalip=<ip>", _("Specify your own public address"));
    strUsage += HelpMessageOpt("-forcednsseed", strprintf(_("Always query for peer addresses via DNS lookup (default: %u)"), 0));
    strUsage += HelpMessageOpt("-listen", _("Accept connections from outside (default: 1 if no -proxy or -connect)"));
    strUsage += HelpMessageOpt("-listenonion", strprintf(_("Automatically create Tor hidden service (default: %d)"), DEFAULT_LISTEN_ONION));
    strUsage += HelpMessageOpt("-maxconnections=<n>", strprintf(_("Maintain at most <n> connections to peers (default: %u)"), 125));
    strUsage += HelpMessageOpt("-maxreceivebuffer=<n>", strprintf(_("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)"), 5000));
    strUsage += HelpMessageOpt("-maxsendbuffer=<n>", strprintf(_("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)"), 1000));
    strUsage += HelpMessageOpt("-onion=<ip:port>", strprintf(_("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: %s)"), "-proxy"));
    strUsage += HelpMessageOpt("-onlynet=<net>", _("Only connect to nodes in network <net> (ipv4, ipv6 or onion)"));
    strUsage += HelpMessageOpt("-permitbaremultisig", strprintf(_("Relay non-P2SH multisig (default: %u)"), 1));
    strUsage += HelpMessageOpt("-peerbloomfilters", strprintf(_("Support filtering of blocks and transaction with bloom filters (default: %u)"), DEFAULT_PEERBLOOMFILTERS));
    strUsage += HelpMessageOpt("-peerbloomfilterszc", strprintf(_("Support the zerocoin light node protocol (default: %u)"), DEFAULT_PEERBLOOMFILTERS_ZC));
    strUsage += HelpMessageOpt("-port=<port>", strprintf(_("Listen for connections on <port> (default: %u or testnet: %u)"), 40444, 55600));
    strUsage += HelpMessageOpt("-proxy=<ip:port>", _("Connect through SOCKS5 proxy"));
    strUsage += HelpMessageOpt("-proxyrandomize", strprintf(_("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)"), 1));
    strUsage += HelpMessageOpt("-seednode=<ip>", _("Connect to a node to retrieve peer addresses, and disconnect"));
    strUsage += HelpMessageOpt("-timeout=<n>", strprintf(_("Specify connection timeout in milliseconds (minimum: 1, default: %d)"), DEFAULT_CONNECT_TIMEOUT));
    strUsage += HelpMessageOpt("-torcontrol=<ip>:<port>", strprintf(_("Tor control port to use if onion listening enabled (default: %s)"), DEFAULT_TOR_CONTROL));
    strUsage += HelpMessageOpt("-torpassword=<pass>", _("Tor control port password (default: empty)"));
#ifdef USE_UPNP
#if USE_UPNP
    strUsage += HelpMessageOpt("-upnp", _("Use UPnP to map the listening port (default: 1 when listening)"));
#else
    strUsage += HelpMessageOpt("-upnp", strprintf(_("Use UPnP to map the listening port (default: %u)"), 0));
#endif
#endif
    strUsage += HelpMessageOpt("-whitebind=<addr>", _("Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-whitelist=<netmask>", _("Whitelist peers connecting from the given netmask or IP address. Can be specified multiple times.") +
            " " + _("Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway"));


#ifdef ENABLE_WALLET
    strUsage += HelpMessageGroup(_("Wallet options:"));
    strUsage += HelpMessageOpt("-backuppath=<dir|file>", _("Specify custom backup path to add a copy of any wallet backup. If set as dir, every backup generates a timestamped file. If set as file, will rewrite to that file every backup."));
    strUsage += HelpMessageOpt("-createwalletbackups=<n>", _("Number of automatic wallet backups (default: 10)"));
    strUsage += HelpMessageOpt("-custombackupthreshold=<n>", strprintf(_("Number of custom location backups to retain (default: %d)"), DEFAULT_CUSTOMBACKUPTHRESHOLD));
    strUsage += HelpMessageOpt("-disablewallet", _("Do not load the wallet and disable wallet RPC calls"));
    strUsage += HelpMessageOpt("-keypool=<n>", strprintf(_("Set key pool size to <n> (default: %u)"), 100));
    if (GetBoolArg("-help-debug", false))
        strUsage += HelpMessageOpt("-mintxfee=<amt>", strprintf(_("Fees (in GIC/Kb) smaller than this are considered zero fee for transaction creation (default: %s)"),
            FormatMoney(CWallet::minTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-paytxfee=<amt>", strprintf(_("Fee (in GIC/kB) to add to transactions you send (default: %s)"), FormatMoney(payTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-rescan", _("Rescan the block chain for missing wallet transactions") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-salvagewallet", _("Attempt to recover private keys from a corrupt wallet.dat") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-sendfreetransactions", strprintf(_("Send transactions as zero-fee transactions if possible (default: %u)"), 0));
    strUsage += HelpMessageOpt("-spendzeroconfchange", strprintf(_("Spend unconfirmed change when sending transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-disablesystemnotifications", strprintf(_("Disable OS notifications for incoming transactions (default: %u)"), 0));
    strUsage += HelpMessageOpt("-txconfirmtarget=<n>", strprintf(_("If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks (default: %u)"), 1));
    strUsage += HelpMessageOpt("-maxtxfee=<amt>", strprintf(_("Maximum total fees to use in a single wallet transaction, setting too low may abort large transactions (default: %s)"),
            FormatMoney(maxTxFee)));
    strUsage += HelpMessageOpt("-upgradewallet", _("Upgrade wallet to latest format") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-wallet=<file>", _("Specify wallet file (within data directory)") + " " + strprintf(_("(default: %s)"), "wallet.dat"));
    strUsage += HelpMessageOpt("-walletnotify=<cmd>", _("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)"));
    if (mode == HMM_BITCOIN_QT)
        strUsage += HelpMessageOpt("-windowtitle=<name>", _("Wallet window title"));
    strUsage += HelpMessageOpt("-zapwallettxes=<mode>", _("Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup") +
            " " + _("(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)"));
#endif

#if ENABLE_ZMQ
    strUsage += HelpMessageGroup(_("ZeroMQ notification options:"));
    strUsage += HelpMessageOpt("-zmqpubhashblock=<address>", _("Enable publish hash block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubhashtx=<address>", _("Enable publish hash transaction in <address>"));
    strUsage += HelpMessageOpt("-zmqpubhashtxlock=<address>", _("Enable publish hash transaction (locked via SwiftX) in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawblock=<address>", _("Enable publish raw block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawtx=<address>", _("Enable publish raw transaction in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawtxlock=<address>", _("Enable publish raw transaction (locked via SwiftX) in <address>"));
#endif

    strUsage += HelpMessageGroup(_("Debugging/Testing options:"));
    strUsage += HelpMessageOpt("-uacomment=<cmt>", _("Append comment to the user agent string"));
    if (GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-checkblockindex", strprintf("Do a full consistency check for mapBlockIndex, setBlockIndexCandidates, chainActive and mapBlocksUnlinked occasionally. Also sets -checkmempool (default: %u)", Params(CBaseChainParams::MAIN).DefaultConsistencyChecks()));
        strUsage += HelpMessageOpt("-checkmempool=<n>", strprintf("Run checks every <n> transactions (default: %u)", Params(CBaseChainParams::MAIN).DefaultConsistencyChecks()));
        strUsage += HelpMessageOpt("-checkpoints", strprintf(_("Only accept block chain matching built-in checkpoints (default: %u)"), 1));
        strUsage += HelpMessageOpt("-dblogsize=<n>", strprintf(_("Flush database activity from memory pool to disk log every <n> megabytes (default: %u)"), 100));
        strUsage += HelpMessageOpt("-disablesafemode", strprintf(_("Disable safemode, override a real safe mode event (default: %u)"), 0));
        strUsage += HelpMessageOpt("-testsafemode", strprintf(_("Force safe mode (default: %u)"), 0));
        strUsage += HelpMessageOpt("-dropmessagestest=<n>", _("Randomly drop 1 of every <n> network messages"));
        strUsage += HelpMessageOpt("-fuzzmessagestest=<n>", _("Randomly fuzz 1 of every <n> network messages"));
        strUsage += HelpMessageOpt("-flushwallet", strprintf(_("Run a thread to flush wallet periodically (default: %u)"), 1));
        strUsage += HelpMessageOpt("-maxreorg", strprintf(_("Use a custom max chain reorganization depth (default: %u)"), 100));
        strUsage += HelpMessageOpt("-stopafterblockimport", strprintf(_("Stop running after importing blocks from disk (default: %u)"), 0));
        strUsage += HelpMessageOpt("-sporkkey=<privkey>", _("Enable spork administration functionality with the appropriate private key."));
    }
    string debugCategories = "addrman, alert, bench, coindb, db, lock, rand, rpc, selectcoins, tor, mempool, net, proxy, http, libevent, giant, (obfuscation, swiftx, masternode, mnpayments, mnbudget, zero, precompute, staking)"; // Don't translate these and qt below
    if (mode == HMM_BITCOIN_QT)
        debugCategories += ", qt";
    strUsage += HelpMessageOpt("-debug=<category>", strprintf(_("Output debugging information (default: %u, supplying <category> is optional)"), 0) + ". " +
            _("If <category> is not supplied, output all debugging information.") + _("<category> can be:") + " " + debugCategories + ".");
    if (GetBoolArg("-help-debug", false))
        strUsage += HelpMessageOpt("-nodebug", "Turn off debugging messages, same as -debug=0");
#ifdef ENABLE_WALLET
    strUsage += HelpMessageOpt("-gen", strprintf(_("Generate coins (default: %u)"), 0));
    strUsage += HelpMessageOpt("-genproclimit=<n>", strprintf(_("Set the number of threads for coin generation if enabled (-1 = all cores, default: %d)"), 1));
#endif
    strUsage += HelpMessageOpt("-help-debug", _("Show all debugging options (usage: --help -help-debug)"));
    strUsage += HelpMessageOpt("-logips", strprintf(_("Include IP addresses in debug output (default: %u)"), 0));
    strUsage += HelpMessageOpt("-logtimestamps", strprintf(_("Prepend debug output with timestamp (default: %u)"), 1));
    if (GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-limitfreerelay=<n>", strprintf(_("Continuously rate-limit free transactions to <n>*1000 bytes per minute (default:%u)"), 15));
        strUsage += HelpMessageOpt("-relaypriority", strprintf(_("Require high priority for relaying free or low-fee transactions (default:%u)"), 1));
        strUsage += HelpMessageOpt("-maxsigcachesize=<n>", strprintf(_("Limit size of signature cache to <n> entries (default: %u)"), 50000));
    }
    strUsage += HelpMessageOpt("-minrelaytxfee=<amt>", strprintf(_("Fees (in GIC/Kb) smaller than this are considered zero fee for relaying (default: %s)"), FormatMoney(::minRelayTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-printtoconsole", strprintf(_("Send trace/debug info to console instead of debug.log file (default: %u)"), 0));
    if (GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-printpriority", strprintf(_("Log transaction priority and fee per kB when mining blocks (default: %u)"), 0));
        strUsage += HelpMessageOpt("-privdb", strprintf(_("Sets the DB_PRIVATE flag in the wallet db environment (default: %u)"), 1));
        strUsage += HelpMessageOpt("-regtest", _("Enter regression test mode, which uses a special chain in which blocks can be solved instantly.") + " " +
                _("This is intended for regression testing tools and app development.") + " " +
                _("In this mode -genproclimit controls how many blocks are generated immediately."));
    }
    strUsage += HelpMessageOpt("-shrinkdebugfile", _("Shrink debug.log file on client startup (default: 1 when no -debug)"));
    strUsage += HelpMessageOpt("-testnet", _("Use the test network"));
    strUsage += HelpMessageOpt("-litemode=<n>", strprintf(_("Disable all GIANT specific functionality (Masternodes, Zerocoin, SwiftX, Budgeting) (0-1, default: %u)"), 0));

#ifdef ENABLE_WALLET
    strUsage += HelpMessageGroup(_("Staking options:"));
    strUsage += HelpMessageOpt("-staking=<n>", strprintf(_("Enable staking functionality (0-1, default: %u)"), 1));
    strUsage += HelpMessageOpt("-gicstake=<n>", strprintf(_("Enable or disable staking functionality for GIC inputs (0-1, default: %u)"), 1));
    strUsage += HelpMessageOpt("-zgicstake=<n>", strprintf(_("Enable or disable staking functionality for zGIC inputs (0-1, default: %u)"), 1));
    strUsage += HelpMessageOpt("-reservebalance=<amt>", _("Keep the specified amount available for spending at all times (default: 0)"));
    if (GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-printstakemodifier", _("Display the stake modifier calculations in the debug.log file."));
        strUsage += HelpMessageOpt("-printcoinstake", _("Display verbose coin stake messages in the debug.log file."));
    }
#endif

    strUsage += HelpMessageGroup(_("Masternode options:"));
    strUsage += HelpMessageOpt("-masternode=<n>", strprintf(_("Enable the client to act as a masternode (0-1, default: %u)"), 0));
    strUsage += HelpMessageOpt("-mnconf=<file>", strprintf(_("Specify masternode configuration file (default: %s)"), "masternode.conf"));
    strUsage += HelpMessageOpt("-mnconflock=<n>", strprintf(_("Lock masternodes from masternode configuration file (default: %u)"), 1));
    strUsage += HelpMessageOpt("-masternodeprivkey=<n>", _("Set the masternode private key"));
    strUsage += HelpMessageOpt("-masternodeaddr=<n>", strprintf(_("Set external address:port to get to this masternode (example: %s)"), "128.127.106.235:40444"));
    strUsage += HelpMessageOpt("-budgetvotemode=<mode>", _("Change automatic finalized budget voting behavior. mode=auto: Vote for only exact finalized budget match to my generated budget. (string, default: auto)"));

    strUsage += HelpMessageGroup(_("Zerocoin options:"));
#ifdef ENABLE_WALLET
    strUsage += HelpMessageOpt("-enablezeromint=<n>", strprintf(_("Enable automatic Zerocoin minting (0-1, default: %u)"), 1));
    strUsage += HelpMessageOpt("-enableautoconvertaddress=<n>", strprintf(_("Enable automatic Zerocoin minting from specific addresses (0-1, default: %u)"), DEFAULT_AUTOCONVERTADDRESS));
    strUsage += HelpMessageOpt("-zeromintpercentage=<n>", strprintf(_("Percentage of automatically minted Zerocoin  (1-100, default: %u)"), 10));
    strUsage += HelpMessageOpt("-preferredDenom=<n>", strprintf(_("Preferred Denomination for automatically minted Zerocoin  (1/5/10/50/100/500/1000/5000), 0 for no preference. default: %u)"), 0));
    strUsage += HelpMessageOpt("-backupzgic=<n>", strprintf(_("Enable automatic wallet backups triggered after each zGIC minting (0-1, default: %u)"), 1));
    strUsage += HelpMessageOpt("-precompute=<n>", strprintf(_("Enable precomputation of zGIC spends and stakes (0-1, default %u)"), 1));
    strUsage += HelpMessageOpt("-precomputecachelength=<n>", strprintf(_("Set the number of included blocks to precompute per cycle. (minimum: %d) (maximum: %d) (default: %d)"), MIN_PRECOMPUTE_LENGTH, MAX_PRECOMPUTE_LENGTH, DEFAULT_PRECOMPUTE_LENGTH));
    strUsage += HelpMessageOpt("-zgicbackuppath=<dir|file>", _("Specify custom backup path to add a copy of any automatic zGIC backup. If set as dir, every backup generates a timestamped file. If set as file, will rewrite to that file every backup. If backuppath is set as well, 4 backups will happen"));
#endif // ENABLE_WALLET
    strUsage += HelpMessageOpt("-reindexzerocoin=<n>", strprintf(_("Delete all zerocoin spends and mints that have been recorded to the blockchain database and reindex them (0-1, default: %u)"), 0));

    //    strUsage += "  -anonymizegiantamount=<n>     " + strprintf(_("Keep N GIC anonymized (default: %u)"), 0) + "\n";
    //    strUsage += "  -liquidityprovider=<n>       " + strprintf(_("Provide liquidity to Obfuscation by infrequently mixing coins on a continual basis (0-100, default: %u, 1=very frequent, high fees, 100=very infrequent, low fees)"), 0) + "\n";

    strUsage += HelpMessageGroup(_("SwiftX options:"));
    strUsage += HelpMessageOpt("-enableswifttx=<n>", strprintf(_("Enable SwiftX, show confirmations for locked transactions (bool, default: %s)"), "true"));
    strUsage += HelpMessageOpt("-swifttxdepth=<n>", strprintf(_("Show N confirmations for a successfully locked transaction (0-9999, default: %u)"), nSwiftTXDepth));

    strUsage += HelpMessageGroup(_("Node relay options:"));
    strUsage += HelpMessageOpt("-datacarrier", strprintf(_("Relay and mine data carrier transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-datacarriersize", strprintf(_("Maximum size of data in data carrier transactions we relay and mine (default: %u)"), MAX_OP_RETURN_RELAY));
    if (GetBoolArg("-help-debug", false)) {
        strUsage += HelpMessageOpt("-blockversion=<n>", "Override block version to