#include "addeditluxnode.h"
#include "qt/forms/ui_addeditluxnode.h"

#include <../wallet/walletdb.h>
#include "wallet/wallet.h"
#include "ui_interface.h"
#include "rpc/util.h"
#include "key.h"
#include "script/script.h"
#include "init.h"
#include "base58.h"
#include "guiutil.h"

#include <QMessageBox>
#include <QSettings>
#include "key_io.h"
#include "net.h"

class CWallet;

AddEditLuxNode::AddEditLuxNode(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AddEditLuxNode)
{
    ui->setupUi(this);

	QSettings settings;

	if (settings.value("theme").toString() == "dark grey") {
		QString styleSheet = "QDialog { background-color: #262626; color: #fff; }"
								".QLabel { color: #fff; }"
								".QLineEdit { background-color: #262626; color:#fff; border: 1px solid #40c2dc; "
								"padding-left:10px; padding-right:10px; min-height:25px; }"
								".QPushButton { background-color: #262626; color:#fff; "
								"border: 1px solid #40c2dc; padding-left:10px; "
								"padding-right:10px; min-height:25px; min-width:75px; }"
								".QPushButton::hover { background-color:#40c2dc; color:#262626; }";
								
		this->setStyleSheet(styleSheet);
		ui->label->setStyleSheet(styleSheet);
		ui->label_2->setStyleSheet(styleSheet);
		ui->label_3->setStyleSheet(styleSheet);
		ui->statusLabel->setStyleSheet(styleSheet);
		ui->aliasLineEdit->setStyleSheet(styleSheet);
		ui->addressLineEdit->setStyleSheet(styleSheet);
		ui->cancelButton->setStyleSheet(styleSheet);
		ui->okButton->setStyleSheet(styleSheet);
		
	} else if (settings.value("theme").toString() == "dark blue") {
		QString styleSheet = "QDialog { background-color: #061532; color: #fff; }"
								".QLabel { color: #fff; }"
								".QLineEdit { background-color: #061532; color:#fff; border: 1px solid #40c2dc; "
								"padding-left:10px; padding-right:10px; min-height:25px; }"
								".QPushButton { background-color: #061532; color:#fff; "
								"border: 1px solid #40c2dc; padding-left:10px; "
								"padding-right:10px; min-height:25px; min-width:75px; }"
								".QPushButton::hover { background-color:#40c2dc; color:#061532; }";
		this->setStyleSheet(styleSheet);
		ui->label->setStyleSheet(styleSheet);
		ui->label_2->setStyleSheet(styleSheet);
		ui->label_3->setStyleSheet(styleSheet);
		ui->statusLabel->setStyleSheet(styleSheet);
		ui->aliasLineEdit->setStyleSheet(styleSheet);
		ui->addressLineEdit->setStyleSheet(styleSheet);
		ui->cancelButton->setStyleSheet(styleSheet);
		ui->okButton->setStyleSheet(styleSheet);
		
	} else { 
		//code here
	}

}

AddEditLuxNode::~AddEditLuxNode()
{
    delete ui;
}


void AddEditLuxNode::on_okButton_clicked()
{
    std::string wallet_name = "";
    std::shared_ptr <CWallet> wallet = GetWallet(wallet_name);
    CWallet *const pwallet = wallet.get();

    if(ui->aliasLineEdit->text() == "")
    {
	QMessageBox msg;
        msg.setText("Please enter an alias.");
	msg.exec();
	return;
    }
    else if(ui->addressLineEdit->text() == "")
    {
	QMessageBox msg;
        msg.setText("Please enter an address.");
	msg.exec();
	return;
    }
    else
    {
        CLuxNodeConfig c;
        c.sAlias = ui->aliasLineEdit->text().toStdString();
        c.sAddress = ui->addressLineEdit->text().toStdString();
        CKey secret;
        secret.MakeNewKey(false);
        c.sMasternodePrivKey = CBitcoinSecret(secret).ToString();

        WalletDatabase& dbh = pwallet->GetDBHandle();
        WalletBatch walletdb(dbh);
        CAccount account;
        walletdb.ReadAccount(c.sAlias, account);
        bool bKeyUsed = false;
        bool bForceNew = false;
        if (account.vchPubKey.IsValid())
        {
            CScript scriptPubKey = GetScriptForDestination(account.vchPubKey.GetID());
            for (std::map<uint256, CWalletTx>::iterator it = pwallet->mapWallet.begin();
                 it != pwallet->mapWallet.end() && account.vchPubKey.IsValid();
                 ++it)
            {
                const CWalletTx& wtx = (*it).second;
                for (const CTxOut& txout : wtx.tx->vout)
                    if (txout.scriptPubKey == scriptPubKey)
                        bKeyUsed = true;
            }
        }

        // Generate a new key
        if (!account.vchPubKey.IsValid() || bForceNew || bKeyUsed)
        {
            if (!pwallet->GetKeyFromPool(account.vchPubKey, false))
            {
                QMessageBox msg;
                msg.setText("Keypool ran out, please call keypoolrefill first.");
                msg.exec();
                return;
            }
            pwallet->SetAddressBook(account.vchPubKey.GetID(), c.sAlias, "");
            walletdb.WriteAccount(c.sAlias, account);

            walletdb.WriteName(EncodeDestination(account.vchPubKey.GetID()), c.sAlias);

        }
        c.sCollateralAddress = EncodeDestination(account.vchPubKey.GetID());

        pwallet->mapMyLuxNodes.insert(make_pair(c.sAddress, c));
        walletdb.WriteLuxNodeConfig(c.sAddress, c);
        uiInterface.NotifyLuxNodeChanged(c);

        accept();
    }
}

void AddEditLuxNode::on_cancelButton_clicked()
{
    reject();
}

