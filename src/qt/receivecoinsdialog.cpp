// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <qt/receivecoinsdialog.h>
#include <qt/forms/ui_receivecoinsdialog.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/receiverequestdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/walletmodel.h>

#include <QAction>
#include <QCursor>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>
//for levelDb
#include <cassert>
#include "leveldb/db.h"
#include <leveldb/c.h>
#include <sstream>
#include <string>
#include "QString"
#include <iostream>
#include <qt/intro.h>
#include <QInputDialog>

#include "HTTPRequest.hpp"

ReceiveCoinsDialog::ReceiveCoinsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ReceiveCoinsDialog),
    columnResizingFixer(0),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    if (!_platformStyle->getImagesOnButtons()) {
        ui->clearButton->setIcon(QIcon());
        ui->receiveButton->setIcon(QIcon());
        ui->showRequestButton->setIcon(QIcon());
        ui->removeRequestButton->setIcon(QIcon());
    } else {
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->receiveButton->setIcon(_platformStyle->SingleColorIcon(":/icons/receiving_addresses"));
        ui->showRequestButton->setIcon(_platformStyle->SingleColorIcon(":/icons/edit"));
        ui->removeRequestButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
    }

    // context menu actions
    QAction *copyURIAction = new QAction(tr("Copy URI"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyMessageAction = new QAction(tr("Copy message"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyURIAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyMessageAction);
    contextMenu->addAction(copyAmountAction);

    // context menu signals
    connect(ui->recentRequestsView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
    connect(copyURIAction, SIGNAL(triggered()), this, SLOT(copyURI()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyMessageAction, SIGNAL(triggered()), this, SLOT(copyMessage()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));
}

void ReceiveCoinsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        _model->getRecentRequestsTableModel()->sort(RecentRequestsTableModel::Date, Qt::DescendingOrder);
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        QTableView* tableView = ui->recentRequestsView;

        tableView->verticalHeader()->hide();
        tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tableView->setModel(_model->getRecentRequestsTableModel());
        tableView->setAlternatingRowColors(true);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);
        tableView->setColumnWidth(RecentRequestsTableModel::Date, DATE_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Label, LABEL_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);

        connect(tableView->selectionModel(),
            SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this,
            SLOT(recentRequestsView_selectionChanged(QItemSelection, QItemSelection)));
        // Last 2 columns are set by the columnResizingFixer, when the table geometry is ready.
        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, AMOUNT_MINIMUM_COLUMN_WIDTH, DATE_COLUMN_WIDTH, this);

        if (model->wallet().getDefaultAddressType() == OutputType::BECH32) {
            ui->useBech32->setCheckState(Qt::Checked);
        } else {
            ui->useBech32->setCheckState(Qt::Unchecked);
        }

        // eventually disable the main receive button if private key operations are disabled
        ui->receiveButton->setEnabled(!model->privateKeysDisabled());
    }
}

ReceiveCoinsDialog::~ReceiveCoinsDialog()
{
    delete ui;
}

void ReceiveCoinsDialog::clear()
{
    ui->reqAmount->clear();
    ui->reqLabel->setText("");
    ui->reqMessage->setText("");
    updateDisplayUnit();
}

void ReceiveCoinsDialog::reject()
{
    clear();
}

void ReceiveCoinsDialog::accept()
{
    clear();
}

void ReceiveCoinsDialog::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        ui->reqAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

void ReceiveCoinsDialog::on_receiveButton_clicked() {

    /*bool check_db = dbEmptyCheck();
    if (!check_db) {
        saveMlcKey();
    }else {*/
        if (!model || !model->getOptionsModel() || !model->getAddressTableModel() ||
            !model->getRecentRequestsTableModel())
            return;

        QString address;
        QString label = ui->reqLabel->text();
        /* Generate new receiving address */
        OutputType address_type;
        if (ui->useBech32->isChecked()) {
            address_type = OutputType::BECH32;
        } else {
            address_type = model->wallet().getDefaultAddressType();
            if (address_type == OutputType::BECH32) {
                address_type = OutputType::P2SH_SEGWIT;
            }
        }
        address = model->getAddressTableModel()->addRow(AddressTableModel::Receive, label, "", address_type);
        SendCoinsRecipient info(address, label,
                                ui->reqAmount->value(), ui->reqMessage->text());
        ReceiveRequestDialog *dialog = new ReceiveRequestDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setModel(model);
        dialog->setInfo(info);
        dialog->show();
        clear();

        /* Store request for later reference */
        model->getRecentRequestsTableModel()->addNewRequest(info);
   // }
}

bool ReceiveCoinsDialog::dbEmptyCheck() {
    std::string std_data_dir = GetDataDir().string();
    //my KeyDb
    leveldb::DB *db_my;
    leveldb::Options options_my;
    options_my.create_if_missing = true;
    std::string StringKey = "StringKey";
    //check that this key already added
    std::string valueToCheck="";
    //open db.
    leveldb::Status status_my = leveldb::DB::Open(options_my, std_data_dir + "/myKey", &db_my);
    if (status_my.ok()) status_my = db_my->Get(leveldb::ReadOptions(), StringKey, &valueToCheck);
    if(valueToCheck == ""){
        delete db_my;
        return false;
    } else {
        delete db_my;
        return true;
    }
}

void ReceiveCoinsDialog::saveMlcKey() {
    std::string std_data_dir = GetDataDir().string();
    bool ok;
    // Ask for sponsor's key of a sponsor that is already joined.
    //input dialog to get sponsor's key.
    QString text = QInputDialog::getText(0, "Input dialog",
                                         "You can't create transaction first give sponsor's key of your sponsor:",
                                         QLineEdit::Normal, "", &ok);
    if (ok) {
        if (text.isEmpty()) {
            QMessageBox Msg_box;
            Msg_box.setText("Please Enter Key First");
            Msg_box.exec();
        } else {
            if (!model || !model->getOptionsModel() || !model->getAddressTableModel() ||
                !model->getRecentRequestsTableModel())
                return;
            if (!model->validateAddress(text)) {
                QMessageBox Msg_box;
                Msg_box.setText("Given Address is Invalid");
                Msg_box.exec();
                return;
            } else {
                try {
                    std::string parent_key = text.toStdString();
                    QString address;
                    OutputType address_type;
                    address_type = OutputType::LEGACY;
                    address = model->getAddressTableModel()->addRow(AddressTableModel::Receive, "", "", address_type);
                    std::string key = address.toStdString();


                    http::Request request("http://nexalt.nexalt.io/index.php?page=is_parent&parent=" + parent_key+"&user="+key+"");
                    const http::Response response = request.send("GET");
                   //std::cout << std::string(response.body.begin(), response.body.end()) << '\n';

                    if(std::string(response.body.begin(), response.body.end()) =="y"){
                       //std::cout<<"return y\n";

                        leveldb::Status status;
                        leveldb::Status status_my;
                        //mlc db
                        leveldb::DB *db;
                        leveldb::Options options;
                        options.create_if_missing = true;
                        //my KeyDb
                        leveldb::DB *db_my;
                        leveldb::Options options_my;
                        options_my.create_if_missing = true;
                        std::string StringKey = "StringKey";

                        if (key != "" && parent_key != "") {
                           //std::cout << "saving key\n";
                            status = leveldb::DB::Open(options, std_data_dir + "/mlcDB", &db);
                            status_my = leveldb::DB::Open(options_my, std_data_dir + "/myKey", &db_my);
                            if (status.ok()) status = db->Put(leveldb::WriteOptions(), key, parent_key);
                            if (status_my.ok()) status_my = db_my->Put(leveldb::WriteOptions(), StringKey, key);
                            delete db;
                            delete db_my;
                            return;
                        }
                    } else{
                        QMessageBox Msg_box;
                        Msg_box.setText("This is not Sponsors key please add valid sponsor key");
                        Msg_box.exec();
                    }
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Request failed, error: " << e.what() << '\n';
                }

                /*
                leveldb::Status status;
                leveldb::Status status_my;
                //mlc db
                leveldb::DB *db;
                leveldb::Options options;
                options.create_if_missing = true;
                //my KeyDb
                leveldb::DB *db_my;
                leveldb::Options options_my;
                options_my.create_if_missing = true;

                std::string value = text.toStdString();
                std::string StringKey = "StringKey";
                //check that this is sponsor already
                std::string valueToCheck;
                //open db.
                status = leveldb::DB::Open(options, std_data_dir + "/mlcDB", &db);

                if (status.ok()) status = db->Get(leveldb::ReadOptions(), value, &valueToCheck);
                if (valueToCheck == "") {
                    //this key is not of sponsor
                    delete db;
                    QMessageBox Msg_box;
                    Msg_box.setText("This is not Sponsors key please add valid sponsor key");
                    Msg_box.exec();
                } else {
                    QString address;
                    OutputType address_type;
                    address_type = OutputType::LEGACY;
                    address = model->getAddressTableModel()->addRow(AddressTableModel::Receive, "", "", address_type);
                    std::string key = address.toStdString();
                    if (key != "" && value != "") {
                        std::cout << "saving key\n";
                        status_my = leveldb::DB::Open(options_my, std_data_dir + "/myKey", &db_my);
                        if (status.ok()) status = db->Put(leveldb::WriteOptions(), key, value);
                        if (status_my.ok()) status_my = db_my->Put(leveldb::WriteOptions(), StringKey, key);
                        delete db;
                        delete db_my;
                        return;
                    } else {
                        delete db;
                    }
                }*/

            }
        }
    }
}

void ReceiveCoinsDialog::on_recentRequestsView_doubleClicked(const QModelIndex &index)
{
    const RecentRequestsTableModel *submodel = model->getRecentRequestsTableModel();
    ReceiveRequestDialog *dialog = new ReceiveRequestDialog(this);
    dialog->setModel(model);
    dialog->setInfo(submodel->entry(index.row()).recipient);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void ReceiveCoinsDialog::recentRequestsView_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    // Enable Show/Remove buttons only if anything is selected.
    bool enable = !ui->recentRequestsView->selectionModel()->selectedRows().isEmpty();
    ui->showRequestButton->setEnabled(enable);
    ui->removeRequestButton->setEnabled(enable);
}

void ReceiveCoinsDialog::on_showRequestButton_clicked()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return;
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();

    for (const QModelIndex& index : selection) {
        on_recentRequestsView_doubleClicked(index);
    }
}

void ReceiveCoinsDialog::on_removeRequestButton_clicked()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return;
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();
    if(selection.empty())
        return;
    // correct for selection mode ContiguousSelection
    QModelIndex firstIndex = selection.at(0);
    model->getRecentRequestsTableModel()->removeRows(firstIndex.row(), selection.length(), firstIndex.parent());
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void ReceiveCoinsDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(RecentRequestsTableModel::Message);
}

void ReceiveCoinsDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return)
    {
        // press return -> submit form
        if (ui->reqLabel->hasFocus() || ui->reqAmount->hasFocus() || ui->reqMessage->hasFocus())
        {
            event->ignore();
            on_receiveButton_clicked();
            return;
        }
    }

    this->QDialog::keyPressEvent(event);
}

QModelIndex ReceiveCoinsDialog::selectedRow()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return QModelIndex();
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();
    if(selection.empty())
        return QModelIndex();
    // correct for selection mode ContiguousSelection
    QModelIndex firstIndex = selection.at(0);
    return firstIndex;
}

// copy column of selected row to clipboard
void ReceiveCoinsDialog::copyColumnToClipboard(int column)
{
    QModelIndex firstIndex = selectedRow();
    if (!firstIndex.isValid()) {
        return;
    }
    GUIUtil::setClipboard(model->getRecentRequestsTableModel()->data(firstIndex.child(firstIndex.row(), column), Qt::EditRole).toString());
}

// context menu
void ReceiveCoinsDialog::showMenu(const QPoint &point)
{
    if (!selectedRow().isValid()) {
        return;
    }
    contextMenu->exec(QCursor::pos());
}

// context menu action: copy URI
void ReceiveCoinsDialog::copyURI()
{
    QModelIndex sel = selectedRow();
    if (!sel.isValid()) {
        return;
    }

    const RecentRequestsTableModel * const submodel = model->getRecentRequestsTableModel();
    const QString uri = GUIUtil::formatBitcoinURI(submodel->entry(sel.row()).recipient);
    GUIUtil::setClipboard(uri);
}

// context menu action: copy label
void ReceiveCoinsDialog::copyLabel()
{
    copyColumnToClipboard(RecentRequestsTableModel::Label);
}

// context menu action: copy message
void ReceiveCoinsDialog::copyMessage()
{
    copyColumnToClipboard(RecentRequestsTableModel::Message);
}

// context menu action: copy amount
void ReceiveCoinsDialog::copyAmount()
{
    copyColumnToClipboard(RecentRequestsTableModel::Amount);
}
