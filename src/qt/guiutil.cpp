
// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "guiutil.h"

#include "bitcoinaddressvalidator.h"
#include "bitcoinunits.h"
#include "qvalidatedlineedit.h"
#include "walletmodel.h"

#include "init.h"
#include "main.h"
#include "primitives/transaction.h"
#include "protocol.h"
#include "script/script.h"
#include "script/standard.h"
#include "util.h"

#ifdef WIN32
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501
#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501
#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "shellapi.h"
#include "shlobj.h"
#include "shlwapi.h"
#endif

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#if BOOST_FILESYSTEM_VERSION >= 3
#include <boost/filesystem/detail/utf8_codecvt_facet.hpp>
#endif

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFont>
#include <QLineEdit>
#include <QSettings>
#include <QTextDocument> // for Qt::mightBeRichText
#include <QThread>
#include <QUrlQuery>
#include <QMouseEvent>


#if BOOST_FILESYSTEM_VERSION >= 3
static boost::filesystem::detail::utf8_codecvt_facet utf8;
#endif

#if defined(Q_OS_MAC)
extern double NSAppKitVersionNumber;
#if !defined(NSAppKitVersionNumber10_8)
#define NSAppKitVersionNumber10_8 1187
#endif
#if !defined(NSAppKitVersionNumber10_9)
#define NSAppKitVersionNumber10_9 1265
#endif
#endif

#define URI_SCHEME "giant"

namespace GUIUtil
{
QString dateTimeStr(const QDateTime& date)
{
    return date.date().toString(Qt::SystemLocaleShortDate) + QString(" ") + date.toString("hh:mm");
}

QString dateTimeStr(qint64 nTime)
{
    return dateTimeStr(QDateTime::fromTime_t((qint32)nTime));
}

QFont bitcoinAddressFont()
{
    QFont font("Monospace");
    font.setStyleHint(QFont::Monospace);
    return font;
}

void setupAddressWidget(QValidatedLineEdit* widget, QWidget* parent)
{
    parent->setFocusProxy(widget);

    widget->setFont(bitcoinAddressFont());
    // We don't want translators to use own addresses in translations
    // and this is the only place, where this address is supplied.
    widget->setPlaceholderText(QObject::tr("Enter a GIANT address (e.g. %1)").arg("D7VFR83SQbiezrW72hjcWJtcfip5krte2Z"));
    widget->setValidator(new BitcoinAddressEntryValidator(parent));
    widget->setCheckValidator(new BitcoinAddressCheckValidator(parent));
}

void setupAmountWidget(QLineEdit* widget, QWidget* parent)
{
    QDoubleValidator* amountValidator = new QDoubleValidator(parent);
    amountValidator->setDecimals(8);
    amountValidator->setBottom(0.0);
    widget->setValidator(amountValidator);
    widget->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

bool parseBitcoinURI(const QUrl& uri, SendCoinsRecipient* out)
{
    // return if URI is not valid or is no GIANT: URI
    if (!uri.isValid() || uri.scheme() != QString(URI_SCHEME))
        return false;

    SendCoinsRecipient rv;
    rv.address = uri.path();
    // Trim any following forward slash which may have been added by the OS
    if (rv.address.endsWith("/")) {
        rv.address.truncate(rv.address.length() - 1);
    }
    rv.amount = 0;

    QUrlQuery uriQuery(uri);
    QList<QPair<QString, QString> > items = uriQuery.queryItems();
    for (QList<QPair<QString, QString> >::iterator i = items.begin(); i != items.end(); i++)
    {
        bool fShouldReturnFalse = false;
        if (i->first.startsWith("req-")) {
            i->first.remove(0, 4);
            fShouldReturnFalse = true;
        }

        if (i->first == "label") {
            rv.label = i->second;
            fShouldReturnFalse = false;
        }
        if (i->first == "message") {
            rv.message = i->second;
            fShouldReturnFalse = false;
        } else if (i->first == "amount") {
            if (!i->second.isEmpty()) {
                if (!BitcoinUnits::parse(BitcoinUnits::GIC, i->second, &rv.amount)) {
                    return false;
                }
            }
            fShouldReturnFalse = false;
        }

        if (fShouldReturnFalse)
            return false;
    }
    if (out) {
        *out = rv;
    }
    return true;
}

bool parseBitcoinURI(QString uri, SendCoinsRecipient* out)
{
    // Convert giant:// to giant:
    //
    //    Cannot handle this later, because giant:// will cause Qt to see the part after // as host,
    //    which will lower-case it (and thus invalidate the address).
    if (uri.startsWith(URI_SCHEME "://", Qt::CaseInsensitive)) {
        uri.replace(0, std::strlen(URI_SCHEME) + 3, URI_SCHEME ":");
    }
    QUrl uriInstance(uri);
    return parseBitcoinURI(uriInstance, out);
}

QString formatBitcoinURI(const SendCoinsRecipient& info)
{
    QString ret = QString(URI_SCHEME ":%1").arg(info.address);
    int paramCount = 0;

    if (info.amount) {
        ret += QString("?amount=%1").arg(BitcoinUnits::format(BitcoinUnits::GIC, info.amount, false, BitcoinUnits::separatorNever));
        paramCount++;
    }

    if (!info.label.isEmpty()) {
        QString lbl(QUrl::toPercentEncoding(info.label));
        ret += QString("%1label=%2").arg(paramCount == 0 ? "?" : "&").arg(lbl);
        paramCount++;
    }
