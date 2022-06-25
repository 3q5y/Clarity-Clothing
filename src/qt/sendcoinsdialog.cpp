// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sendcoinsdialog.h"
#include "ui_sendcoinsdialog.h"

#include "addresstablemodel.h"
#include "askpassphrasedialog.h"
#include "bitcoinunits.h"
#include "clientmodel.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "sendcoinsentry.h"
#include "walletmodel.h"

#include "base58.h"
#include "coincontrol.h"
#include "guiinterface.h"
#include "utilmoneystr.h"
#include "wallet/wallet.h"

#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>

SendCoinsDialog::SendCoinsDialog(QWidget* parent) : QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
                                                    ui(new Ui::SendCoinsDialog),
                                                    clientModel(0),
                                                    model(0),
                                                    fNewRecipientAllowed(true),
                                                    fFeeMinimized(true)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->addButton->setIcon(QIcon());
    ui->clearButton->setIcon(QIcon());
    ui->sendButton->setIcon(QIcon());
#endif

    GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);

    addEntry();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // Coin Control
    connect(ui->pushButtonCoinControl, SIGNAL(clicked()), this, SLOT(coinControlButtonClicked()));
    connect(ui->checkBoxCoinControlChange, SIGNAL(stateChanged(int)), this, SLOT(coinControlChangeChecked(int)));
    connect(ui->lineEditCoinControlChange, SIGNAL(textEdited(const QString&)), this, SLOT(coinControlChangeEdited(const QString&)));

    // UTXO Splitter
    connect(ui->splitBlockCheckBox, SIGNAL(stateChanged(int)), this, SLOT(splitBlockChecked(int)));
    connect(ui->splitBlockLineEdit, SIGNAL(textChanged(const QString&)), this, SLOT(splitBlockLineEditChanged(const QString&)));

    // GIANT specific
    QSettings settings;
    if (!settings.contains("bUseObfuScation"))
        settings.setValue("bUseObfuScation", false);
    if (!settings.contains("bUseSwiftTX"))
        settings.setValue("bUseSwiftTX", false);

    bool useSwiftTX = settings.value("bUseSwiftTX").toBool();
    if (fLiteMode) {
        ui->checkSwiftTX->setVisible(false);
        CoinControlDialog::coinControl->useObfuScation = false;
        CoinControlDialog::coinControl->useSwiftTX = false;
    } else {
        ui->checkSwiftTX->setChecked(useSwiftTX);
        CoinControlDialog::coinControl->useSwiftTX = useSwiftTX;
    }

    connect(ui->checkSwiftTX, SIGNAL(stateChanged(int)), this, SLOT(updateSwiftTX()));

    // Coin Control: clipboard actions
    QAction* clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction* clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction* clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction* clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction* clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction* clipboardPriorityAction = new QAction(tr("Copy priority"), this);
    QAction* clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction* clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardBytes()));
    connect(clipboardPriorityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardPriority()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardChange()));
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlPriority->addAction(clipboardPriorityAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

    // init transaction fee section
    if (!settings.contains("fFeeSectionMinimized"))
        settings.setValue("fFeeSectionMinimized", true);
    if (!settings.contains("nFeeRadio") && settings.contains("nTransactionFee") && settings.value("nTransactionFee").toLongLong() > 0) // compatibility
        settings.setValue("nFeeRadio", 1);                                                                                             // custom
    if (!settings.contains("nFeeRadio"))
        settings.setValue("nFeeRadio", 0);                                                                                                   // recommended
    if (!settings.contains("nCustomFeeRadio") && settings.contains("nTransactionFee") && settings.value("nTransactionFee").toLongLong() > 0) // compatibility
        settings.setValue("nCustomFeeRadio", 1);                                                                                             // total at least
    if (!settings.contains("nCustomFeeRadio"))
        settings.setValue("nCustomFeeRadio", 0); // per kilobyte
    if (!settings.contains("nSmartFeeSliderPosition"))
        settings.setValue("nSmartFeeSliderPosition", 0);
    if (!settings.contains("nTransactionFee"))
        settings.setValue("nTransactionFee", (qint64)DEFAULT_TRANSACTION_FEE);
    if (!settings.contains("fPayOnlyMinFee"))
        settings.setValue("fPayOnlyMinFee", false);
    if (!settings.contains("fSendFreeTransactions"))
        settings.setValue("fSendFreeTransactions", false);

    ui->groupFee->setId(ui->radioSmartFee, 0);
    ui->groupFee->setId(ui->radioCustomFee, 1);
    ui->groupFee->button((int)std::max(0, std::min(1, settings.value("nFeeRadio").toInt())))->setChecked(true);
    ui->groupCustomFee->setId(ui->radioCustomPerKilobyte, 0);
    ui->groupCustomFee->setId(ui->radioCustomAtLeast, 1);
    ui->groupCustomFee->button((int)std::max(0, std::min(1, settings.value("nCustomFeeRadio").toInt())))->setChecked(true);
    ui->sliderSmartFee->setValue(settings.value("nSmartFeeSliderPosition").toInt());
    ui->customFee->setValue(settings.value("nTransactionFee").toLongLong());
    ui->checkBoxMinimumFee->setChecked(settings.value("fPayOnlyMinFee").toBool());
    ui->checkBoxFreeTx->setChecked(settings.value("fSendFreeTransactions").toBool());
    ui->checkzGIC->hide();

    minimizeFeeSection(settings.value("fFeeSectionMinimized").toBool());
    // If SwiftX activated hide button 'Choose'. Show otherwise.
    ui->buttonChooseFee->setVisible(!useSwiftTX);
}

void SendCoinsDialog::setClientModel(ClientModel* clientModel)
{
    this->clientModel = clientModel;

    if (clientModel) {
        connect(clientModel, SIGNAL(numBlocksChanged(int)), this, SLOT(updateSmartFeeLabel()));
    }
}

void SendCoinsDialog::setModel(WalletModel* model)
{
    this->model = model;

    if (model && model->getOptionsModel()) {
        for (int i = 0; i < ui->entries->count(); ++i) {
            SendCoinsEntry* entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
            if (entry) {
                entry->setModel(model);
            }
        }

        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   model->getZerocoinBalance (), model->getUnconfirmedZerocoinBalance (), model->getImmatureZerocoinBalance (),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance());
        connect(model, SIGNAL(balanceChanged(CAmount, CAmount,  CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)), this,
                         SLOT(setBalance(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Coin Control
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(coinControlFeatureChanged(bool)));
        ui->frameCoinControl->setVisible(model->getOptionsModel()->getCoinControlFeatures());
        coinControlUpdateLabels();

        // fee section
        connect(ui->sliderSmartFee, SIGNAL(valueChanged(int)), this, SLOT(updateSmartFeeLabel()));
        connect(ui->sliderSmartFee, SIGNAL(valueChanged(int)), this, SLOT(updateGlobalFeeVariables()));
        connect(ui->sliderSmartFee, SIGNAL(valueChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateGlobalFeeVariables()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->groupCustomFee, SIGNAL(buttonClicked(int)), this, SLOT(updateGlobalFeeVariables()));
        connect(ui->groupCustomFee, SIGNAL(buttonClicked(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(updateGlobalFeeVariables()));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(coinControlUpdateLabels()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateGlobalFeeVariables()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->checkBoxFreeTx, SIGNAL(stateChanged(int)), this, SLOT(updateGlobalFeeVariables()));
        connect(ui->checkBoxFreeTx, SIGNAL(stateChanged(int)), this, SLOT(coinControlUpdateLabels()));
        ui->customFee->setSingleStep(CWallet::minTxFee.GetFeePerK());
        updateFeeSectionControls();
        updateMinFeeLabel();
        updateSmartFeeLabel();
        updateGlobalFeeVariables();
    }
}

SendCoinsDialog::~SendCoinsDialog()
{
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nCustomFeeRadio", ui->groupCustomFee->checkedId());
    settings.setValue("nSmartFeeSliderPosition", ui->sliderSmartFee->value());
    settings.setValue("nTransactionFee", (qint64)ui->customFee->value());
    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());
    settings.setValue("fSendFreeTransactions", ui->checkBoxFreeTx->isChecked());

    delete ui;
}

void SendCoinsDialog::on_sendButton_clicked()
{
    if (!model || !model->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    for (int i = 0; i < ui->entries->count(); ++i) {
        SendCoinsEntry* entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());

        //UTXO splitter - address should be our own
        CBitcoinAddress address = entry->getValue().address.toStdString();
        if (!model->isMine(address) && ui->splitBlockCheckBox->checkState() == Qt::Checked) {
            CoinControlDialog::coinControl->fSplitBlock = false;
            ui->splitBlockCheckBox->setCheckState(Qt::Unchecked);
            QMessageBox::warning(this, tr("Send Coins"),
                tr("The split block tool does not work when sending to outside addresses. Try again."),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }

        if (entry) {
            if (entry->validate()) {
                recipients.append(entry->getValue());
            } else {
                valid = false;
            }
        }
    }

    if (!valid || recipients.isEmpty()) {
        return;
    }

    //set split block in model
    CoinControlDialog::coinControl->fSplitBlock = ui->splitBlockCheckBox->checkState() == Qt::Checked;

    if (ui->entries->count() > 1 && ui->splitBlockCheckBox->checkState() == Qt::Checked) {
        CoinControlDialog::coinControl->fSplitBlock = false;
        ui->splitBlockCheckBox->setCheckState(Qt::Unchecked);
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The split block tool does not work with multiple addresses. Try again."),
            QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    if (CoinControlDialog::coinControl->fSplitBlock)
        CoinControlDialog::coinControl->nSplitBlock = int(ui->splitBlockLineEdit->text().toInt());

    QString strFunds = "";
    QString strFee = "";
    recipients[0].inputType = ALL_COINS;

    if (ui->checkSwiftTX->isChecked()) {
        recipients[0].useSwiftTX = true;
        strFunds += " ";
        strFunds += tr("using SwiftX");
    } else {
        recipients[0].useSwiftTX = false;
    }


    // Format confirmation message
    QStringList formatted;
    foreach (const SendCoinsRecipient& rcp, recipients) {
        // generate bold amount string
        QString amount = "<b>" + BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), rcp.amount);
        amount.append("</b> ").append(strFunds);

        // generate monospace address string
        QString address = "<span style='font-family: monospace;'>" + rcp.address;
        address.append("</span>");

        QString recipientElement;

        if (!rcp.paymentRequest.IsInitialized()) // normal payment
        {
            if (rcp.label.length() > 0) // label with address
            {
                recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.label));
                recipientElement.append(QString(" (%1)").arg(address));
            } else // just address
            {
                recipientElement = tr("%1 to %2").arg(amount, address);
            }
        } else if (!rcp.authenticatedMerchant.isEmpty()) // secure payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.authenticatedMerchant));
        } else // insecure payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, address);
        }

        if (CoinControlDialog::coinControl->fSplitBlock) {
            recipientElement.append(tr(" split into %1 outputs using the UTXO splitter.").arg(CoinControlDialog::coinControl->nSplitBlock));
        }

        formatted.append(recipientElement);
    }

    fNewRecipientAllowed = false;

    // request unlock only if was locked or unlocked for mixing:
    // this way we let users unlock by walletpassphrase or by menu
    // and make many transactions while unlocking through this dialog
    // will call relock
    WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
    if (encStatus == model->Locked || encStatus == model->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(model->requestUnlock(AskPassphraseDialog::Context::Send_GIC, true));
        if (!ctx.isValid()) {
            // Unlock wallet was cancelled
            fNewRecipientAllowed = true;
            return;
        }
        send(recipients, strFee, formatted);
        return;
    }
    // already unlocked or not encrypted at all
    send(recipients, strFee, formatted);
}

void SendCoinsDialog::send(QList<SendCoinsRecipient> recipients, QString strFee, QStringList formatted)
{
    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients);
    WalletModel::SendCoinsReturn prepareStatus;
    if (model->getOptionsModel()->getCoinControlFeatures()) // coin control enabled
        prepareStatus = model->prepareTransaction(currentTransaction, CoinControlDialog::coinControl);
    else
        prepareStatus = model->prepareTransaction(currentTransaction);

    // process prepareStatus and on error generate message shown to user
    processSendCoinsReturn(prepareStatus,
        BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), currentTransaction.getTransactionFee()), true);

    if (prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        return;
    }

    CAmount txFee = currentTransaction.getTransactionFee();
    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />%1");

    if (txFee > 0) {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("are added as transaction fee"));
        questionString.append(" ");
        questionString.append(strFee);

        // append transaction size
        questionString.append(" (" + QString::number((double)currentTransaction.getTransactionSize() / 1000) + " kB)");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;
    QStringList alternativeUnits;
    foreach (BitcoinUnits::Unit u, BitcoinUnits::availableUnits()) {
        if (u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(BitcoinUnits::formatHtmlWithUnit(u, totalAmount));
    }

    // Show total amount + all alternative units
    questionString.append(tr("Total Amount = <b>%1</b><br />= %2")
                              .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount))
                              .arg(alternativeUnits.join("<br />= ")));

    // Limit number of displayed entries
    int messageEntries = formatted.size();
    int displayedEntries = 0;
    for (int i = 0; i < formatted.size(); i++) {
        if (i >= MAX_SEND_POPUP_ENTRIES) {
            formatted.removeLast();
            i--;
        } else {
            displayedEntries = i + 1;
        }
    }
    questionString.append("<hr />");
    questionString.append(tr("<b>(%1 of %2 entries displayed)</b>").arg(displayedEntries).arg(messageEntries));

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send coins"),
        questionString.arg(formatted.join("<br 