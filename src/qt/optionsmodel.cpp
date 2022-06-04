// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/giant-config.h"
#endif

#include "optionsmodel.h"

#include "bitcoinunits.h"
#include "guiutil.h"

#include "amount.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "txdb.h" // for -dbcache defaults
#include "util.h"

#ifdef ENABLE_WALLET
#include "masternodeconfig.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif

#include <QNetworkProxy>
#include <QSettings>
#include <QStringList>

OptionsModel::OptionsModel(QObject* parent) : QAbstractListModel(parent)
{
    Init();
}

void OptionsModel::addOverriddenOption(const std::string& option)
{
    strOverriddenByCommandLine += QString::fromStdString(option) + "=" + QString::fromStdString(mapArgs[option]) + " ";
}

// Writes all missing QSettings with their default values
void OptionsModel::Init()
{
    resetSettings = false;
    QSettings settings;

    // Ensure restart flag is unset on client startup
    setRestartRequired(false);

    // These are Qt-only settings:

    // Window
    if (!settings.contains("fMinimizeToTray"))
        settings.setValue("fMinimizeToTray", false);
    fMinimizeToTray = settings.value("fMinimizeToTray").toBool();

    if (!settings.contains("fMinimizeOnClose"))
        settings.setValue("fMinimizeOnClose", false);
    fMinimizeOnClose = settings.value("fMinimizeOnClose").toBool();

    // Display
    if (!settings.contains("nDisplayUnit"))
        settings.setValue("nDisplayUnit", BitcoinUnits::GIC);
    nDisplayUnit = settings.value("nDisplayUnit").toInt();

    if (!settings.contains("strThirdPartyTxUrls"))
        settings.setValue("strThirdPartyTxUrls", "");
    strThirdPartyTxUrls = settings.value("strThirdPartyTxUrls", "").toString();

    if (!settings.contains("fHideZeroBalances"))
        settings.setValue("fHideZeroBalances", true);
    fHideZeroBalances = settings.value("fHideZeroBalances").toBool();

    if (!settings.contains("fHideOrphans"))
        settings.setValue("fHideOrphans", false);
    fHideOrphans = settings.value("fHideOrphans").toBool();

    if (!settings.contains("fCoinControlFeatures"))
        settings.setValue("fCoinControlFeatures", false);
    fCoinControlFeatures = settings.value("fCoinControlFeatures", false).toBool();

    if (!settings.contains("fZeromintEnable"))
        settings.setValue("fZeromintEnable", true);
    fEnableZeromint = settings.value("fZeromintEnable").toBool();

    if (!settings.contains("fEnableAutoConvert"))
        settings.setValue("fEnableAutoConvert", true);
    fEnableAutoConvert = settings.value("fEnableAutoConvert").toBool();

    if (!settings.contains("nZeromintPercentage"))
        settings.setValue("nZeromintPercentage", 10);
    nZeromintPercentage = settings.value("nZeromintPercentage").toLongLong();

    if (!settings.contains("nPreferredDenom"))
        settings.setValue("nPreferredDenom", 0);
    nPreferredDenom = settings.value("nPreferredDenom", "0").toLongLong();

    if (!settings.contains("nAnonymizeGiantAmount"))
        settings.setValue("nAnonymizeGiantAmount", 1000);

    nAnonymizeGiantAmount = settings.value("nAnonymizeGiantAmount").toLongLong();

    if (!settings.contains("fShowMasternodesTab"))
        settings.setValue("fShowMasternodesTab", masternodeConfig.getCount());

    // These are shared with the core or have a command-line parameter
    // and we want command-line parameters to overwrite the GUI settings.
    //
    // If setting doesn't exist create it with defaults.
    //
    // If SoftSetArg() or SoftSetBoolArg() return false we were overridden
    // by command-line and show this in the UI.

    // Main
    if (!settings.contains("nDatabaseCache"))
        settings.setValue("nDatabaseCache", (qint64)nDefaultDbCache);
    if (!SoftSetArg("-dbcache", settings.value("nDatabaseCache").toString().toStdString()))
        addOverriddenOption("-dbcache");

    if (!settings.contains("nThreadsScriptVerif"))
        settings.setValue("nThreadsScriptVerif", DEFAULT_SCRIPTCHECK_THREADS);
    if (!SoftSetArg("-par", settings.value("nThreadsScriptVerif").toString().toStdString()))
        addOverriddenOption("-par");

// Wallet
#ifdef ENABLE_WALLET
    if (!settings.contains("bSpendZeroConfChange"))
        settings.setValue("bSpendZeroConfChange", false);
    if (!SoftSetBoolArg("-spendzeroconfchange", settings.value("bSpendZeroConfChange").toBool()))
        addOverriddenOption("-spendzeroconfchange");
#endif
    if (!settings.contains("nStakeSplitThreshold"))
        settings.setValue("nStakeSplitThreshold", 1);


    // Network
    if (!settings.contains("fUseUPnP"))
        settings.setValue("fUseUPnP", DEFAULT_UPNP);
    if (!SoftSetBoolArg("-upnp", settings.value("fUseUPnP").toBool()))
        addOverriddenOption("-upnp");

    if (!settings.contains("fListen"))
        settings.setValue("fListen", DEFAULT_LISTEN);
    if (!SoftSetBoolArg("-listen", settings.value("fListen").toBool()))
        addOverriddenOption("-listen");

    if (!settings.contains("fUseProxy"))
        settings.setValue("fUseProxy", false);
    if (!settings.contains("addrProxy"))
        settings.setValue("addrProxy", "127.0.0.1:9050");
    // Only try to set -proxy, if user has enabled fUseProxy
    if (settings.value("fUseProxy").toBool() && !SoftSetArg("-proxy", settings.value("addrProxy").toString().toStdString()))
        addOverriddenOption("-proxy");
    else if (!settings.value("fUseProxy").toBool() && !GetArg("-proxy", "").empty())
        addOverriddenOption("-proxy");

    // Display
    if (!settings.contains("digits"))
        settings.setValue("digits", "2");
    if (!settings.contains("theme"))
        settings.setValue("theme", "");
    if (!settings.contains("fCSSexternal"))
        settings.setValue("fCSSexternal", false);
    if (!settings.contains("language"))
        settings.setValue("language", "");
    if (!SoftSetArg("-lang", settings.value("language").toString().toStdString()))
        addOverriddenOption("-lang");

    if (settings.contains("fZeromintEnable"))
        SoftSetBoolArg("-enablezeromint", settings.value("fZeromintEnable").toBool());
    if (setti