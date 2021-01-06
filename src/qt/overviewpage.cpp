// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>

#include <QAbstractItemDelegate>
#include <QPainter>

#include <qt/sendcoinsdialog.h>
#include <cassert>
#include "leveldb/db.h"
#include <leveldb/c.h>
#include <sstream>
#include <string>
#include "QString"
#include <iostream>
#include <qt/intro.h>
#include <QInputDialog>
#include <validation.h>
#include <core_io.h>
#include <univalue.h>
#include <rpc/server.h>
#include <validationinterface.h>
#include <validation.h>
#include <future>
#include <consensus/validation.h>

#include <chainparams.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <wallet/coincontrol.h>
#include <ui_interface.h>
#include <txmempool.h>
#include <policy/fees.h>
#include <wallet/fees.h>

using namespace std;

vector <string> explode_raw_overview(const string &delimiter, const string &explodeme);

vector <string> explode_raw_overview(const string &delimiter, const string &str) {
    vector <string> arr;

    int strleng = str.length();
    int delleng = delimiter.length();
    if (delleng == 0)
        return arr;//no change

    int i = 0;
    int k = 0;
    while (i < strleng) {
        int j = 0;
        while (i + j < strleng && j < delleng && str[i + j] == delimiter[j])
            j++;
        if (j == delleng)//found delimiter
        {
            arr.push_back(str.substr(k, i - k));
            i += delleng;
            k = i;
        } else {
            i++;
        }
    }
    arr.push_back(str.substr(k, i - k));
    return arr;
}

int hex_value_overview(char hex_digit) {
    switch (hex_digit) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return hex_digit - '0';

        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
            return hex_digit - 'A' + 10;

        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
            return hex_digit - 'a' + 10;
    }
    throw std::invalid_argument("invalid hex digit");
}

string stringToHex_overview(string str) {
    static const char hex_digits[] = "0123456789abcdef";
    //std::string input="12X4p9Yeptio7CjngqzmTu1QYhbizvHizrmJ*3e1ff0005354536084005f5944fd69c786431b4c2a841ea0ba07e7087b554868";
    std::string output;
    output.reserve(str.length() * 2);
    for (unsigned char c : str) {
        output.push_back(hex_digits[c >> 4]);
        output.push_back(hex_digits[c & 15]);
    }
    return output;
}

string hexToString_overview(string str) {
    const auto len = str.length();
    if (len & 1) throw std::invalid_argument("odd length");

    std::string output_string;
    output_string.reserve(len / 2);
    for (auto it = str.begin(); it != str.end();) {
        int hi = hex_value_overview(*it++);
        int lo = hex_value_overview(*it++);
        output_string.push_back(hi << 4 | lo);
    }
    return output_string;
}

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

Q_DECLARE_METATYPE(interfaces::WalletBalances)

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::BTC),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(nullptr),
    walletModel(nullptr),
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);

    m_balances.balance = -1;

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, &QListView::clicked, this, &OverviewPage::handleTransactionClicked);

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, &QPushButton::clicked, this, &OverviewPage::handleOutOfSyncWarningClicks);
    connect(ui->labelTransactionsStatus, &QPushButton::clicked, this, &OverviewPage::handleOutOfSyncWarningClicks);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const interfaces::WalletBalances& balances)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    m_balances = balances;
    if (walletModel->privateKeysDisabled()) {
        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balances.watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, balances.unconfirmed_watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balances.watch_only_balance + balances.unconfirmed_watch_only_balance + balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));
    } else {
        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balances.balance, false, BitcoinUnits::separatorAlways));
        ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, balances.unconfirmed_balance, false, BitcoinUnits::separatorAlways));
        ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, balances.immature_balance, false, BitcoinUnits::separatorAlways));
        ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balances.balance + balances.unconfirmed_balance + balances.immature_balance, false, BitcoinUnits::separatorAlways));
        ui->labelWatchAvailable->setText(BitcoinUnits::formatWithUnit(unit, balances.watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(unit, balances.unconfirmed_watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelWatchImmature->setText(BitcoinUnits::formatWithUnit(unit, balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(unit, balances.watch_only_balance + balances.unconfirmed_watch_only_balance + balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));
    }
    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = balances.immature_balance != 0;
    bool showWatchOnlyImmature = balances.immature_watch_only_balance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(!walletModel->privateKeysDisabled() && showWatchOnlyImmature); // show watch-only immature balance

    const QString name_wallet = walletModel->getWalletName();
    std::string walletName = name_wallet.toStdString();
    bool isEmpty = isDbEmpty(walletName);
    if(isEmpty){
        ui->buttontoaddkey->setVisible(true);
    }else{
        ui->buttontoaddkey->setVisible(false);
    }
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly)
        ui->labelWatchImmature->hide();
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, &ClientModel::alertsChanged, this, &OverviewPage::updateAlerts);
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        interfaces::Wallet& wallet = model->wallet();
        interfaces::WalletBalances balances = wallet.getBalances();
        setBalance(balances);
        connect(model, &WalletModel::balanceChanged, this, &OverviewPage::setBalance);

        connect(model->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &OverviewPage::updateDisplayUnit);

        updateWatchOnlyLabels(wallet.haveWatchOnly() && !model->privateKeysDisabled());
        connect(model, &WalletModel::notifyWatchonlyChanged, [this](bool showWatchOnly) {
            updateWatchOnlyLabels(showWatchOnly && !walletModel->privateKeysDisabled());
        });
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();

    const QString name_wallet = walletModel->getWalletName();
    std::string walletName = name_wallet.toStdString();
    bool isEmpty = isDbEmpty(walletName);
    if(isEmpty){
        ui->buttontoaddkey->setVisible(true);
    }else{
        ui->buttontoaddkey->setVisible(false);
    }

}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if (m_balances.balance != -1) {
            setBalance(m_balances);
        }

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::on_buttontoaddkey_clicked() {
    const QString name_wallet = walletModel->getWalletName();
    std::string walletName = name_wallet.toStdString();
    bool isEmpty = isDbEmpty(walletName);
    if(isEmpty){
        bool KeySaved = KeySaving(walletName);
        if(KeySaved){
            ui->buttontoaddkey->setVisible(false);
        }
    }else{
        ui->buttontoaddkey->setVisible(false);
        QMessageBox Msg_box;
        Msg_box.setText("key is already added");
        Msg_box.exec();
    }
}

bool OverviewPage::isDbEmpty(std::string wallet_name) {
    std::string std_data_dir = GetDataDir().string();
    leveldb::DB *db_my;
    leveldb::Options options_my;
    options_my.create_if_missing = true;
    std::string valueToCheck = "";
    std::string value = "";
    if(wallet_name == ""){
        //default wallet
        std::string StringKeyToShow = "StringKeyToShow";
        std::string StringKey = "StringKey";
        leveldb::Status status_my = leveldb::DB::Open(options_my, std_data_dir + "/myKey", &db_my);
        if (status_my.ok()) status_my = db_my->Get(leveldb::ReadOptions(), StringKeyToShow, &valueToCheck);
        if (status_my.ok()) status_my = db_my->Get(leveldb::ReadOptions(), StringKey, &value);
        delete db_my;
        if(valueToCheck == "" && value ==""){
            return true;
        }else{
            return false;
        }
    }else{
        std::string wallet_name_show = wallet_name +"_show";
        leveldb::Status status_my = leveldb::DB::Open(options_my, std_data_dir + "/myKey", &db_my);
        if (status_my.ok()) status_my = db_my->Get(leveldb::ReadOptions(), wallet_name_show, &valueToCheck);
        if (status_my.ok()) status_my = db_my->Get(leveldb::ReadOptions(), wallet_name, &value);
        delete db_my;
        if(valueToCheck == "" && value ==""){
            return true;
        }else{
            return false;
        }
    }
}

bool OverviewPage::KeySaving(std::string wallet_name) {
    bool ok;
    QString text = QInputDialog::getText(0, "Input dialog",
                                         "You can't create transaction add sponsor key First:",
                                         QLineEdit::Normal, "", &ok);

    if (ok) {
        if (text.isEmpty()) {
            QMessageBox Msg_box;
            Msg_box.setText("Please Enter Key First");
            Msg_box.exec();
            return false;
        } else {
            std::string hashString = text.toStdString();
            uint256 hash;
            hash.SetHex(hashString);
            CBlockIndex *blockindex = nullptr;

            if (hash == Params().GenesisBlock().hashMerkleRoot) {
                QMessageBox Msg_box;
                Msg_box.setText(
                        "The genesis block CoinBase is not considered an ordinary transaction and cannot be retrieved");
                Msg_box.exec();
                return false;
            }
            bool f_txindex_ready = false;
            std::string valueToSave = "";
            CTransactionRef txone;
            uint256 hash_block;
            if (!GetTransaction(hash, txone, Params().GetConsensus(), hash_block, blockindex)) {
                std::string errmsg;
                if (blockindex) {
                    if (!(blockindex->nStatus & BLOCK_HAVE_DATA)) {
                        errmsg = "Block not available";
                    }
                    errmsg = "No such transaction found in the provided block";
                } else if (!f_txindex_ready) {
                    errmsg = "No such mempool transaction. Blockchain transactions are still in the process of being indexed";
                } else {
                    errmsg = "No such mempool or blockchain transaction";
                }
                //throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg + ". Use gettransaction for wallet transactions.");
                QMessageBox Msg_box;
                Msg_box.setText("inValid Transaction"+ QString::fromLocal8Bit(errmsg.c_str()));
                Msg_box.exec();
                return false;
            }
            UniValue result(UniValue::VOBJ);
            UniValue vout(UniValue::VARR);
            const CTransaction &tx = *txone;
            string hexconverted = "";
            bool is_op_return_trans = false;

            for (unsigned int i = 0; i < tx.vout.size(); i++) {
                const CTxOut &txout = tx.vout[i];
                UniValue out(UniValue::VOBJ);
                UniValue o(UniValue::VOBJ);
                ScriptPubKeyToUniv(txout.scriptPubKey, o, true);

                txnouttype type;
                std::vector <CTxDestination> addresses;
                int nRequired;
                std::string asmstring = ScriptToAsmStr(txout.scriptPubKey);
                std::vector <std::string> no_keys = explode_raw_overview(" ", asmstring);

                if (!is_op_return_trans) {
                    if (no_keys[0] == "OP_RETURN") {
                        hexconverted = hexToString_overview(no_keys[1]);

                        std::string mlc = hexconverted.substr(0, 3);
                        std::string KoT = hexconverted.substr(3, 1);

                        std::string key = hexconverted.substr(4, 34);
                        std::string value = hexconverted.substr(38, 34);

                        if (mlc == "MLC") {
                            if (KoT == "K") {
                                std::string std_data_dir = GetDataDir().string();
                                leveldb::Status status;
                                leveldb::DB *db;
                                leveldb::Options options;
                                options.create_if_missing = true;
                                std::string valueToCheck = "";

                                status = leveldb::DB::Open(options, std_data_dir + "/mlc", &db);
                                if (status.ok()) status = db->Get(leveldb::ReadOptions(), key, &valueToCheck);
                                delete db;
                                if (valueToCheck == "") {
                                    QMessageBox Msg_box;
                                    Msg_box.setText("This key is Not Valid please add a valid key");
                                    Msg_box.exec();
                                    return false;
                                } else {
                                    valueToSave = key;
                                    is_op_return_trans = true;
                                }
                            } else {
                                QMessageBox Msg_box;
                                Msg_box.setText("This key is Not Valid please add a valid key");
                                Msg_box.exec();
                                return false;
                            }
                        } else {
                            QMessageBox Msg_box;
                            Msg_box.setText("This key is Not Valid please add a valid key");
                            Msg_box.exec();
                            return false;
                        }
                    } else {
                        QMessageBox Msg_box;
                        Msg_box.setText("This key is Not Valid please add a valid key");
                        Msg_box.exec();
                        return false;
                    }
                }
            }

            std::shared_ptr <CWallet> wallet = GetWallet(wallet_name);
            CWallet *const pwallet = wallet.get();

            int nMinDepth = 1;
            int nMaxDepth = 9999999;
            std::set <CTxDestination> destinations;
            bool include_unsafe = true;
            CAmount nMinimumAmount = 0;
            CAmount nMaximumAmount = MAX_MONEY;
            CAmount nMinimumSumAmount = MAX_MONEY;
            uint64_t nMaximumCount = 0;

            std::vector <COutput> vecOutputs;
            {
                auto locked_chain = pwallet->chain().lock();
                LOCK2(cs_main, pwallet->cs_wallet);
                pwallet->AvailableCoins(*locked_chain,vecOutputs, !include_unsafe, nullptr, nMinimumAmount, nMaximumAmount,
                                        nMinimumSumAmount,
                                        nMaximumCount, nMinDepth, nMaxDepth);
            }
            uint256 txid;
            CScript scriptpubkeymine;
            CAmount amountdeduct;
            std::string mlcAddress = "";
            int voutunspent = 0;

            for (const COutput &out : vecOutputs) {
                CTxDestination address;
                const CScript &scriptPubKey = out.tx->tx->vout[out.i].scriptPubKey;
                bool fValidAddress = ExtractDestination(scriptPubKey, address);
                if (destinations.size() && (!fValidAddress || !destinations.count(address)))
                    continue;

                voutunspent = out.i;

                scriptpubkeymine = scriptPubKey;
                txid = out.tx->GetHash();

                if (fValidAddress) {
                    mlcAddress = EncodeDestination(address);
                }
                amountdeduct = out.tx->tx->vout[out.i].nValue - 2680;

                if (!IsValidDestination(address)) {
                    QMessageBox Msg_box;
                    Msg_box.setText("Invalid Nexalt address");
                    Msg_box.exec();
                    return false;
                }
                auto keyid = GetKeyForDestination(*pwallet, address);
                if (keyid.IsNull()) {
                    QMessageBox Msg_box;
                    Msg_box.setText("Address does not refer to a key");
                    Msg_box.exec();
                    return false;
                }
                CKey vchSecret;
                if (!pwallet->GetKey(keyid, vchSecret)) {
                    QMessageBox Msg_box;
                    Msg_box.setText("Private key for address is not known");
                    Msg_box.exec();
                    return false;
                }
            }

            CMutableTransaction rawTx;
            int nOutput = voutunspent;
            uint32_t nSequence;
            nSequence = std::numeric_limits<uint32_t>::max();
            CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);
            rawTx.vin.push_back(in);

            //creating users sponsor ship key
            CPubKey newKey;
            if (!pwallet->GetKeyFromPool(newKey)) {
                QMessageBox Msg_box;
                Msg_box.setText("Keypool ran out, please call keypoolrefill first");
                Msg_box.exec();
                return false;
            }
            OutputType output_type = OutputType::LEGACY;
            pwallet->LearnRelatedScripts(newKey, output_type);
            CTxDestination dest = GetDestinationForKey(newKey, output_type);
            std::string key = EncodeDestination(dest);

            for (int i = 0; i < 2; i++) {
                if (i == 0) {
                    std::string datatostore = key;
                    std::string mlcDataStore = "MLCK" + datatostore + valueToSave;
                    std::string hexDataToStore = stringToHex_overview(mlcDataStore);
                    std::vector<unsigned char> data = ParseHexV(hexDataToStore, "Data");
                    CTxOut out(0, CScript() << OP_RETURN << data);
                    rawTx.vout.push_back(out);
                } else {
                    CAmount nAmount = amountdeduct;
                    CTxOut out(nAmount, scriptpubkeymine);
                    rawTx.vout.push_back(out);
                }
            }

            std::string rawtransaction = EncodeHexTx(CTransaction(rawTx));

            //raw transaction created and now signing raw transactiomn
            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, rawtransaction, true)) {
                QMessageBox Msg_box;
                Msg_box.setText("TX decode failed");
                Msg_box.exec();
                return false;
            }

            CCoinsView viewDummy;
            CCoinsViewCache view(&viewDummy);
            {
                LOCK2(cs_main, mempool.cs);
                CCoinsViewCache &viewChain = *pcoinsTip;
                CCoinsViewMemPool viewMempool(&viewChain, mempool);
                view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view
                for (const CTxIn &txin : mtx.vin) {
                    view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
                }
                view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
            }

            int nHashType = ParseSighashString("ALL");

            bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);
            // Script verification errors
            UniValue vErrors(UniValue::VARR);
            // Use CTransaction for the constant parts of the
            // transaction to avoid rehashing.
            const CTransaction txConst(mtx);
            // Sign what we can:
            for (unsigned int i = 0; i < mtx.vin.size(); i++) {
                CTxIn &txin = mtx.vin[i];
                const Coin &coin = view.AccessCoin(txin.prevout);
                if (coin.IsSpent()) {
                    QMessageBox Msg_box;
                    Msg_box.setText("Input not found or already spent");
                    Msg_box.exec();
                    return false;
                    //continue;
                }
                const CScript &prevPubKey = coin.out.scriptPubKey;
                const CAmount &amount = coin.out.nValue;

                SignatureData sigdata = DataFromTransaction(mtx, i, coin.out);
                // Only sign SIGHASH_SINGLE if there's a corresponding output:
                if (!fHashSingle || (i < mtx.vout.size())) {
                    ProduceSignature(*pwallet, MutableTransactionSignatureCreator(&mtx, i, amount, nHashType),
                                     prevPubKey,
                                     sigdata);
                }
                UpdateInput(txin, sigdata);

                // amount must be specified for valid segwit signature
                if (amount == MAX_MONEY && !txin.scriptWitness.IsNull()) {
                    throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing amount for %s", coin.out.ToString()));
                }
                ScriptError serror = SCRIPT_ERR_OK;
            }
            bool fComplete = vErrors.empty();

            if (!vErrors.empty()) {
                result.pushKV("errors", vErrors);
                QMessageBox Msg_box;
                Msg_box.setText(" Some Error occured");
                Msg_box.exec();
                return false;
            }
            std::promise<void> promise;

            CMutableTransaction mtxtwo;
            if (!DecodeHexTx(mtxtwo, EncodeHexTx(CTransaction(mtx))))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            CTransactionRef tx_create(MakeTransactionRef(std::move(mtxtwo)));
            const uint256 &hashTx = tx_create->GetHash();

            CAmount nMaxRawTxFee = maxTxFee;

            nMaxRawTxFee = 0;
            { // cs_main scope
                LOCK(cs_main);
                CCoinsViewCache &view = *pcoinsTip;
                bool fHaveChain = false;
                for (size_t o = 0; !fHaveChain && o < tx_create->vout.size(); o++) {
                    const Coin &existingCoin = view.AccessCoin(COutPoint(hashTx, o));
                    fHaveChain = !existingCoin.IsSpent();
                }
                bool fHaveMempool = mempool.exists(hashTx);
                if (!fHaveMempool && !fHaveChain) {
                    // push to local node and sync with wallets
                    CValidationState state;
                    bool fMissingInputs;
                    if (!AcceptToMemoryPool(mempool, state, std::move(tx_create), &fMissingInputs,
                                            nullptr /* plTxnReplaced */, false /* bypass_limits */, nMaxRawTxFee)) {
                        if (state.IsInvalid()) {
                            throw JSONRPCError(RPC_TRANSACTION_REJECTED, FormatStateMessage(state));
                        } else {
                            if (fMissingInputs) {
                                throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                            }
                            throw JSONRPCError(RPC_TRANSACTION_ERROR, FormatStateMessage(state));
                        }
                    } else {
                        // If wallet is enabled, ensure that the wallet has been made aware
                        // of the new transaction prior to returning. This prevents a race
                        // where a user might call sendrawtransaction with a transaction
                        // to/from their wallet, immediately call some wallet RPC, and get
                        // a stale result because callbacks have not yet been processed.
                        CallFunctionInValidationInterfaceQueue([&promise] {
                            promise.set_value();
                        });
                    }
                } else if (fHaveChain) {
                    throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
                } else {
                    // Make sure we don't block forever if re-sending
                    // a transaction already in mempool.
                    promise.set_value();
                }

            } // cs_main
            promise.get_future().wait();

            if (!g_connman)
                throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

            CInv inv(MSG_TX, hashTx);
            g_connman->ForEachNode([&inv](CNode *pnode) {
                pnode->PushInventory(inv);
            });
            std::string KeyFound = hashTx.GetHex();
            QMessageBox Msg_box;
            Msg_box.setText("Sponsor Ship Key: " +QString::fromLocal8Bit(KeyFound.c_str()) );
            Msg_box.exec();
            return true;
        }
    }else{
        return false;
    }
}
