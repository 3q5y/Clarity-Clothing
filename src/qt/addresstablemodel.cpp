// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addresstablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"

#include "base58.h"
#include "wallet/wallet.h"
#include "askpassphrasedialog.h"

#include <QDebug>
#include <QFont>

const QString AddressTableModel::Send = "S";
const QString AddressTableModel::Receive = "R";
const QString AddressTableModel::Zerocoin = "X";

struct AddressTableEntry {
    enum Type {
        Sending,
        Receiving,
        Zerocoin,
        Hidden /* QSortFilterProxyModel will filter these out */
    };

    Type type;
    QString label;
    QString address;
    QString pubcoin;

    AddressTableEntry() {}
    AddressTableEntry(Type type, const QString &pubcoin):    type(type), pubcoin(pubcoin) {}
    AddressTableEntry(Type type, const QString& label, const QString& address) : type(type), label(label), address(address) {}
};

struct AddressTableEntryLessThan {
    bool operator()(const AddressTableEntry& a, const AddressTableEntry& b) const
    {
        return a.address < b.address;
    }
    bool operator()(const AddressTableEntry& a, const QString& b) const
    {
        return a.address < b;
    }
    bool operator()(const QString& a, const AddressTableEntry& b) const
    {
        return a < b.address;
    }
};

/* Determine address type from address purpose */
static AddressTableEntry::Type translateTransactionType(const QString& strPurpose, bool isMine)
{
    AddressTableEntry::Type addressType = AddressTableEntry::Hidden;
    // "refund" addresses aren't shown, and change addresses aren't in mapAddressBook at all.
    if (strPurpose == "send")
        addressType = AddressTableEntry::Sending;
    else if (strPurpose == "r