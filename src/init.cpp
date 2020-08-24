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
// Thread ma