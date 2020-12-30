// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bip38tooldialog.h"
#include "ui_bip38tooldialog.h"

#include "addressbookpage.h"
#include "guiutil.h"
#include "walletmodel.h"

#include "base58.h"
#include "bip38.h"
#include "init.h"
#include "wallet/wallet.h"
#include "askpassphrasedialog.h"

#include <string>
#include <vector>

#include <QClipboard>

Bip38ToolDialog::Bip38ToolDialog(QWidget* parent) : QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
                                                    ui(new Ui::Bip38ToolDialog),
                                                    model(0)
{
    ui->setupUi(this);

    ui->decryptedKeyOut_DEC->setPlaceholderText(tr("Click \"Decrypt Key\" to compute key"));

    GUIUtil::setupAddressWidget(ui->addressIn_ENC, this);
    ui->addressIn_ENC->installEventFilter(this);
    ui->passphraseIn_ENC->installEventFilter(this);
    ui->encryptedKeyOut_ENC->installEventFilter(this);
    ui->encryptedKeyIn_DEC->installEventFilter(this);
    ui->passphraseIn_DEC->installEventFilter(this);
    ui->decryptedKeyOut_DEC->installEventFilter(this);
}

Bip38ToolDialog::~Bip38ToolDialog()
{
    delete ui;
}

void Bip38ToolDialog::setModel(WalletModel* model)
{
    this->model = model;
}

void Bip38ToolDialog::setAddress_ENC(const QString& address)
{
    ui->addressIn_ENC->setText(address);
    ui->passphraseIn_ENC->setFocus();
}

void Bip38ToolDialog::setAddress_DEC(const QString& address)
{
    ui->encryptedKeyIn_DEC->setText(address);
    ui->passphraseIn_DEC->setFocus();
}

void Bip38ToolDialog::showTab_ENC(bool fShow)
{
    ui->tabWidget->setCurrentIndex(0);
    if (fShow)
        this->show();
}

void Bip38ToolDialog::showTab_DEC(bool fShow)
{
    ui->tabWidget->setCurrentIndex(1);
    if (fShow)
        this->show();
}

void Bip38ToolDialog::on_addressBookButton_ENC_clicked()
{
    if (model && model->getAddressTableModel()) {
        AddressBookPage dlg(AddressBookPage::ForSelection, AddressBookPage::ReceivingTab, this);
        dlg.setModel(model->getAddressTableModel());
        if (dlg.exec()) {
            setAddress_ENC(dlg.getReturnValue());
        }
    }
}

void Bip38ToolDialog::on_pasteButton_ENC_clicked()
{
    setAddress_ENC(QApplication::clipboard()->text());
}

QString specialChar = "\"@!#$%&'()*+,-./:;<=>?`{|}~^_[]\\";
QString validChar = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" + specialChar;
bool isValidPassphrase(QString strPassphrase, QString& s