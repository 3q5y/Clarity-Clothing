// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactiondesc.h"

#include "bitcoinunits.h"
#include "guiutil.h"
#include "paymentserver.h"
#include "transactionrecord.h"

#include "base58.h"
#include "db.h"
#include "main.h"
#include "script/script.h"
#include "timedata.h"
#include "guiinterface.h"
#include "util.h"
#include "wallet/wallet.h"

#include <stdint.h>
#include <string>

using namespace std;

QString TransactionDesc::FormatTxStatus(const CWalletTx& wtx)
{
    AssertLockHeld(cs_main);
    if (!IsFinalTx(wtx, chainActive.Height() + 1)) {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD)
            return tr("Open for %n more block(s)", "", wtx.nLockTime - chainActive.Height());
        else
            return tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx.nLockTime));
    } else {
        int signatures = wtx.GetTransactionLockSignatures();
        QString strUsingIX = "";
        if (signatures >= 0) {
            if (signatures >= SWIFTTX_SIGNATURES_REQUIRED) {
                int nDepth = wtx.GetDepthInMainChain();
                if (nDepth < 0)
                    return tr("conflicted");
                else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    return tr("%1/offline (verified via SwiftX)").arg(nDepth);
                else if (nDepth < 6)
                    return tr("%1/confirmed (verified via SwiftX)").arg(nDepth);
                else
                    return tr("%1 confirmations (verified via SwiftX)").arg(nDepth);
            } else {
                if (!wtx.IsTransactionLockTimedOut()) {
                    int nDepth = wtx.GetDepthInMainChain();
                    if (nDepth < 0)
                        return tr("conflicted");
                    else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                        return tr("%1/offline (SwiftX verification in progress - %2 of %3 signatures)").arg(nDepth).arg(signatures).arg(SWIFTTX_SIGNATURES_TOTAL);
                    else if (nDepth < 6)
                        return tr("%1/confirmed (SwiftX verification in progress - %2 of %3 signatures )").arg(nDepth).arg(signatures).arg(SWIFTTX_SIGNATURES_TOTAL);
                    else
                        return tr("%1 confirmations (SwiftX verification in progress - %2 of %3 signatures)").arg(nDepth).arg(signatures).arg(SWIFTTX_SIGNATURES_TOTAL);
                } else {
                    int nDepth = wtx.GetDepthInMainChain();
                    if (nDepth < 0)
                        return tr("conflicted");
                    else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                        return tr("%1/offline (SwiftX verification failed)").arg(nDepth);
                    else if (nDepth < 6)
                        return tr("%1/confirmed (SwiftX verification failed)").arg(nDepth);
                    else
                        return tr("%1 confirmations").arg(nDepth);
                }
            }
        } else {
            int nDepth = wtx.GetDepthInMainChain();
            if (nDepth < 0)
                return tr("conflicted");
            else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                return tr("%1/offline").arg(nDepth);
            else if (nDepth < 6)
                return tr("%1/unconfirmed").arg(nDepth);
            else
                return tr("%1 confirmations").arg(nDepth);
        }
    }
}

QString TransactionDesc::toHTML(CWallet* wallet, CWalletTx& wtx, TransactionRecord* rec, int unit)
{
    QString strHTML;

    LOCK2(cs_main, wallet->cs_wallet);
    strHTML.reserve(4000);
    strHTML += "<html><font face='verdana, arial, helvetica, sans-serif'>";

    CAmount nNet = rec->credit + rec->debit;

    strHTML += "<b>" + tr("Status") + ":</b> " + FormatTxStatus(wtx);
    int nRequests = wtx.GetRequestCount();
    if (nRequests != -1) {
        if (nRequests == 0)
            strHTML += tr(", has not been successfully broadcast yet");
        else if (nRequests > 0)
            strHTML += tr(", broadcast through %n node(s)", "", nRequests);
    }
    strHTML += "<br>";

    strHTML += "<b>" + tr("Date") + ":</b> " + (rec->time ? GUIUtil::dateTimeStr(rec->time) : "") + "<br>";

    //
    // From
    //
    if (wtx.IsCoinBase()) {
        strHTML += "<b>" + tr("Source") + ":</b> " + tr("Generated") + "<br>";
    } else if (wtx.mapValue.count("from") && !wtx.mapValue["from"].empty()) {
        // Online transaction
        strHTML += "<b>" + tr("From") + ":</b> " + GUIUtil::HtmlEscape(wtx.mapValue["from"]) + "<br>";
    } else {
        // Offline transaction
        if (nNet > 0) {
            // Credit
            if (CBitcoinAddress(rec->address).IsValid()) {
                CTxDestination address = CBitcoinAddress(rec->address).Get();
                if (wallet->mapAddressBook.count(address)) {
                    strHTML += "<b>" + tr("From") + ":</b> " + tr("unknown") + "<br>";
                    strHTML += "<b>" + tr("To") + ":</b> ";
                    strHTML += GUIUtil::HtmlEscape(rec->address);
                    QString addressOwned = (::IsMine(*wallet, address) == ISMINE_SPENDABLE) ? tr("own address") : tr("watch-only");
                    if (!wallet->mapAddressBook[address].name.empty())
                        strHTML += " (" + addressOwned + ", " + tr("label") + ": " + GUIUtil::HtmlEscape(wallet->mapAddressBook[address].name) + ")";
                    else
                        strHTML += " (" + addressOwned + ")";
                    strHTML += "<br>";
                }
            }
        }
    }

    //
    // To
    //
    if (wtx.mapValue.count("to") && !wtx.mapValue["to"].empty()) {
        // Online transaction
        std::string strAddress = wtx.mapValue["to"];
        strHTML += "<b>" + tr("To") + ":</b> ";
        CTxDestination dest = CBitcoinAddress(strAddress).Get();
        if (wallet->mapAddressBook.count(dest) && !wallet->mapAddressBook[dest].name.empty())
            strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[dest].name) + " ";
        strHTML += GUIUtil::HtmlEscape(strAddress) + "<br>";
    }

    //
    // Amount
    //
    if (wtx.IsCoinBase() && rec->credit == 0) {
        //
        // Coinbase
        //
        CAmount nUnmatured = 0;
        for (const CTxOut& txout : wtx.vout)
            nUnmatured += wallet->GetCredit(txout, ISMINE_ALL);
        strHTML += "<b>" + tr("Credit") + ":</b> ";
        if (wtx.IsInMainChain())
            strHTML += BitcoinUnits::formatHtmlWithUnit(unit, nUnmatured) + " (" + tr("matures in %n more block(s)", "", wtx.GetBlocksToMaturity()) + ")";
        else
            strHTML += "(" + tr("not accepted") + ")";
        strHTML