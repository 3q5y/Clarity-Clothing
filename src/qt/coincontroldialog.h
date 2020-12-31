// Copyright (c) 2011-2013 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_COINCONTROLDIALOG_H
#define BITCOIN_QT_COINCONTROLDIALOG_H

#include "amount.h"

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTreeWidgetItem>

class WalletModel;

class MultisigDialog;
class CCoinControl;
class CTxMemPool;

namespace Ui
{
class CoinControlDialog;
}

class CCoinControlWidgetItem : public QTreeWidgetItem
{
public:
    explicit CCoinControlWidgetItem(QTreeWidget *parent, int type = Type) : QTreeWidgetItem(parent, type) {}
    explicit CCoinControlWidgetItem(int type = Type) : QTreeWidgetItem(type) {}
    explicit CCoinControlWidgetItem(QTreeWidgetItem *parent, int type = Type) : QTreeWidgetItem(parent, type) {}

    bool operator<(const QTreeWidgetItem &other) const;
};

class CoinControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CoinControlDialog(QWidget* parent = nullptr, bool fMultisigEnabled = false);
    ~CoinControlDialog();

    void setModel(WalletModel* model);
    void updateDialogLabels();

    // static because also called from sendcoinsdialog
    static void updateLabels(WalletModel*, QDialog*);
    static QString getPriorityLabel(double dPriority, double mempoolEstimatePriority);

    static QList<CAmount> payAmounts;
    static CCoinControl* coinControl;
 