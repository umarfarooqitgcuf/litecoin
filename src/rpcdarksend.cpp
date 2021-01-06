// Copyright (c) 2012-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The Luxcore developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#include "main.h"
#include "primitives/transaction.h"
#include "db.h"
#include "init.h"
#include "masternode.h"
#include "activemasternode.h"
#include "masternodeconfig.h"
#include "rpc/server.h"
#include <boost/lexical_cast.hpp>
//#include "amount.h"
#include "rpc/util.h"
#include "util/moneystr.h"
#include "validation.h"
#include <consensus/validation.h>

#include <boost/tokenizer.hpp>

#include <fstream>
#include "key_io.h"
#include "net.h"
#include "wallet/rpcwallet.h"
#include <wallet/coincontrol.h>



void SendMoney(const CTxDestination& address, CAmount nValue, CWalletTx& wtxNew, AvailableCoinsType coin_type) {
    // Check amount
    std::string wallet_name = "";
    std::shared_ptr <CWallet> wallet = GetWallet(wallet_name);
    CWallet *const pwallet = wallet.get();

    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > pwallet->GetBalance())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    string strError;
    if (pwallet->IsLocked()) {
        strError = "Error: Wallet locked, unable to create transaction!";
        LogPrintf("SendMoney() : %s", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    // Parse Nexalt address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwallet);
    CCoinControl coinControl;
    CAmount nFeeRequired;
    std::vector<CRecipient> vecSend;
    CRecipient recipient = {scriptPubKey, nValue, false};
    vecSend.push_back(recipient);
    int nChangePos;
    auto locked_chain = pwallet->chain().lock();
    LOCK(pwallet->cs_wallet);
    if (!pwallet->CreateTransaction(*locked_chain,vecSend, wtxNew.tx, reservekey, nFeeRequired, nChangePos, strError, coinControl/*NULL, coin_type*/)) {
        if (nValue + nFeeRequired > pwallet->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        LogPrintf("SendMoney() : %s\n", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    CValidationState state;
    if (!pwallet->CommitTransaction(wtxNew.tx,{},{}, reservekey,g_connman.get(),state)){
        throw JSONRPCError(RPC_WALLET_ERROR,
                           "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
    }
}

UniValue darksend(const UniValue& params, bool fHelp) {
    std::string wallet_name = "";
    std::shared_ptr <CWallet> wallet = GetWallet(wallet_name);
    CWallet *const pwallet = wallet.get();
    if (fHelp || params.size() == 0)
        throw runtime_error(
                "darksend <Luxaddress> <amount>\n"
                "Luxaddress, reset, or auto (AutoDenominate)"
                "<amount> is a real and is rounded to the nearest 0.00000001"
                + HelpRequiringPassphrase(pwallet));

    if (pwallet->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
    if (params[0].get_str() == "auto") {
        if (fMasterNode)
            return "DarkSend is not supported from masternodes";

        darkSendPool.DoAutomaticDenominating();
        return "DoAutomaticDenominating";
    }

    if (params[0].get_str() == "reset") {
        darkSendPool.SetNull(true);
        darkSendPool.UnlockCoins();
        return "successfully reset darksend";
    }

    if (params.size() != 2)
        throw runtime_error(
                "darksend <Luxaddress> <amount>\n"
                "Luxaddress, denominate, or auto (AutoDenominate)"
                "<amount> is a real and is rounded to the nearest 0.00000001"
                + HelpRequiringPassphrase(pwallet));

    CTxDestination dest = DecodeDestination(params[0].get_str());
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Nexalt address");

    // Amount
    int64_t nAmount = AmountFromValue(params[1]);

    // Wallet comments
    CTransactionRef txRef;

    const CTransaction& tx = *txRef;
    CWalletTx wtx(pwallet,txRef) ;
    SendMoney(dest, nAmount, wtx, ONLY_DENOMINATED);

    return wtx.GetHash().GetHex();
}


UniValue getpoolinfo(const UniValue& params, bool fHelp) {
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getpoolinfo\n"
                "Returns an object containing anonymous pool-related information.");

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("current_masternode", GetCurrentMasterNode());
    obj.pushKV("state", darkSendPool.GetState());
    obj.pushKV("entries", darkSendPool.GetEntriesCount());
    obj.pushKV("entries_accepted", darkSendPool.GetCountEntriesAccepted());
    return obj;
}