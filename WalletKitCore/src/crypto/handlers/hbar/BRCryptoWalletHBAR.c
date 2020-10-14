//
//  BRCryptoWalletHBAR.c
//  Core
//
//  Created by Ehsan Rezaie on 2020-05-19.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include "BRCryptoHBAR.h"
#include "BRCryptoBase.h"
#include "crypto/BRCryptoWalletP.h"
#include "crypto/BRCryptoAmountP.h"
#include "crypto/BRCryptoFeeBasisP.h"
#include "hedera/BRHederaTransaction.h"
#include "support/BRSet.h"
#include "ethereum/util/BRUtilMath.h"
#include <stdio.h>


static BRCryptoWalletHBAR
cryptoWalletCoerce (BRCryptoWallet wallet) {
    assert (CRYPTO_NETWORK_TYPE_HBAR == wallet->type);
    return (BRCryptoWalletHBAR) wallet;
}

typedef struct {
    BRHederaAccount hbarAccount;
} BRCryptoWalletCreateContextHBAR;

static void
cryptoWalletCreateCallbackHBAR (BRCryptoWalletCreateContext context,
                               BRCryptoWallet wallet) {
    BRCryptoWalletCreateContextHBAR *contextHBAR = (BRCryptoWalletCreateContextHBAR*) context;
    BRCryptoWalletHBAR walletHBAR = cryptoWalletCoerce (wallet);

    walletHBAR->hbarAccount = contextHBAR->hbarAccount;
}

private_extern BRCryptoWallet
cryptoWalletCreateAsHBAR (BRCryptoWalletListener listener,
                          BRCryptoWalletFileServiceContext fileServiceContext,
                          BRCryptoUnit unit,
                          BRCryptoUnit unitForFee,
                          BRHederaAccount hbarAccount) {
    int hasMinBalance;
    int hasMaxBalance;
    BRHederaUnitTinyBar minBalance = hederaAccountGetBalanceLimit (hbarAccount, 0, &hasMinBalance);
    BRHederaUnitTinyBar maxBalance = hederaAccountGetBalanceLimit (hbarAccount, 1, &hasMaxBalance);

    BRHederaFeeBasis feeBasisHBAR = hederaAccountGetDefaultFeeBasis (hbarAccount);
    BRCryptoFeeBasis feeBasis     = cryptoFeeBasisCreateAsHBAR (unitForFee, feeBasisHBAR);

    BRCryptoWalletCreateContextHBAR contextHBAR = {
        hbarAccount
    };

    BRCryptoWallet wallet = cryptoWalletAllocAndInit (sizeof (struct BRCryptoWalletHBARRecord),
                                                      CRYPTO_NETWORK_TYPE_HBAR,
                                                      listener,
                                                      fileServiceContext,
                                                      unit,
                                                      unitForFee,
                                                      hasMinBalance ? cryptoAmountCreateAsHBAR(unit, CRYPTO_FALSE, minBalance) : NULL,
                                                      hasMaxBalance ? cryptoAmountCreateAsHBAR(unit, CRYPTO_FALSE, maxBalance) : NULL,
                                                      feeBasis,
                                                      &contextHBAR,
                                                      cryptoWalletCreateCallbackHBAR);
    cryptoFeeBasisGive(feeBasis);
    
    return wallet;
}

//private_extern BRHederaWallet
//cryptoWalletAsHBAR (BRCryptoWallet wallet) {
//    BRCryptoWalletHBAR walletHBAR = cryptoWalletCoerce(wallet);
//    return walletHBAR->wid;
//}

static void
cryptoWalletReleaseHBAR (BRCryptoWallet wallet) {
    BRCryptoWalletHBAR walletHBAR = cryptoWalletCoerce (wallet);
    hederaAccountFree(walletHBAR->hbarAccount);
}

static BRCryptoAddress
cryptoWalletGetAddressHBAR (BRCryptoWallet wallet,
                            BRCryptoAddressScheme addressScheme) {
    assert (CRYPTO_ADDRESS_SCHEME_GEN_DEFAULT == addressScheme);
    BRCryptoWalletHBAR walletHBAR = cryptoWalletCoerce (wallet);
    return cryptoAddressCreateAsHBAR (hederaAccountGetAddress (walletHBAR->hbarAccount));
}

static bool
cryptoWalletHasAddressHBAR (BRCryptoWallet wallet,
                            BRCryptoAddress address) {
    BRCryptoWalletHBAR walletHBAR = cryptoWalletCoerce (wallet);
    
    BRHederaAddress hbarAddress = cryptoAddressAsHBAR (address);
    
    return hederaAccountHasAddress (walletHBAR->hbarAccount, hbarAddress);
}

extern size_t
cryptoWalletGetTransferAttributeCountHBAR (BRCryptoWallet wallet,
                                           BRCryptoAddress target) {
    BRHederaAddress hbarTarget = cryptoAddressAsHBAR (target);
    
    size_t countRequired, countOptional;
    hederaWalletGetTransactionAttributeKeys (hbarTarget, 1, &countRequired);
    hederaWalletGetTransactionAttributeKeys (hbarTarget, 0, &countOptional);
    return countRequired + countOptional;
}

extern BRCryptoTransferAttribute
cryptoWalletGetTransferAttributeAtHBAR (BRCryptoWallet wallet,
                                        BRCryptoAddress target,
                                        size_t index) {
    BRHederaAddress hbarTarget = cryptoAddressAsHBAR (target);
    
    size_t countRequired, countOptional;
    const char **keysRequired = hederaWalletGetTransactionAttributeKeys (hbarTarget, 1, &countRequired);
    const char **keysOptional = hederaWalletGetTransactionAttributeKeys (hbarTarget, 0, &countOptional);
    
    assert (index < (countRequired + countOptional));
    
    BRCryptoBoolean isRequired = AS_CRYPTO_BOOLEAN (index < countRequired);
    const char **keys      = (isRequired ? keysRequired : keysOptional);
    size_t       keysIndex = (isRequired ? index : (index - countRequired));
    
    return cryptoTransferAttributeCreate(keys[keysIndex], NULL, isRequired);
}

extern BRCryptoTransferAttributeValidationError
cryptoWalletValidateTransferAttributeHBAR (BRCryptoWallet wallet,
                                           OwnershipKept BRCryptoTransferAttribute attribute,
                                           BRCryptoBoolean *validates) {
    const char *key = cryptoTransferAttributeGetKey (attribute);
    const char *val = cryptoTransferAttributeGetValue (attribute);
    BRCryptoTransferAttributeValidationError error = 0;
    
    // If attribute.value is NULL, we validate unless the attribute.value is required.
    if (NULL == val) {
        if (cryptoTransferAttributeIsRequired(attribute)) {
            error = CRYPTO_TRANSFER_ATTRIBUTE_VALIDATION_ERROR_REQUIRED_BUT_NOT_PROVIDED;
            *validates = CRYPTO_FALSE;
        } else {
            *validates = CRYPTO_TRUE;
        }
        return error;
    }
    
    if (hederaCompareAttribute (key, TRANSFER_ATTRIBUTE_MEMO_TAG)) {
        // There is no constraint on the form of the 'memo' field.
        *validates = CRYPTO_TRUE;
    }
    else {
        error = CRYPTO_TRANSFER_ATTRIBUTE_VALIDATION_ERROR_RELATIONSHIP_INCONSISTENCY;
        *validates = CRYPTO_FALSE;
    }
    
    return error;
}

extern BRCryptoTransfer
cryptoWalletCreateTransferHBAR (BRCryptoWallet  wallet,
                                BRCryptoAddress target,
                                BRCryptoAmount  amount,
                                BRCryptoFeeBasis estimatedFeeBasis,
                                size_t attributesCount,
                                OwnershipKept BRCryptoTransferAttribute *attributes,
                                BRCryptoCurrency currency,
                                BRCryptoUnit unit,
                                BRCryptoUnit unitForFee) {
    BRCryptoWalletHBAR walletHBAR = cryptoWalletCoerce (wallet);

    BRHederaAddress source = hederaAccountGetAddress(walletHBAR->hbarAccount);
    UInt256 value = cryptoAmountGetValue (amount);
    BRHederaUnitTinyBar thbar = (BRHederaUnitTinyBar) value.u64[0];
    BRHederaAddress nodeAddress = hederaAccountGetNodeAddress (walletHBAR->hbarAccount);
    BRHederaFeeBasis hbarFeeBasis = cryptoFeeBasisCoerceHBAR (estimatedFeeBasis)->hbarFeeBasis;
    
    BRHederaTransaction hbarTransaction = hederaTransactionCreateNew (source,
                                                                      cryptoAddressAsHBAR (target),
                                                                      thbar,
                                                                      hbarFeeBasis,
                                                                      nodeAddress,
                                                                      NULL);
    if (NULL == hbarTransaction)
        return NULL;
    
    for (size_t index = 0; index < attributesCount; index++) {
        BRCryptoTransferAttribute attribute = attributes[index];
        if (NULL != cryptoTransferAttributeGetValue(attribute)) {
            if (hederaCompareAttribute (cryptoTransferAttributeGetKey(attribute), TRANSFER_ATTRIBUTE_MEMO_TAG)) {
                hederaTransactionSetMemo (hbarTransaction, cryptoTransferAttributeGetValue(attribute));
            }
            else {
                // TODO: Impossible if validated?
            }
        }
    }
    
    hederaAddressFree (source);
    hederaAddressFree (nodeAddress);

    uint64_t hbarBlockheight = hederaTransactionGetBlockheight (hbarTransaction);
    uint64_t hbarTimestamp   = (uint64_t) hederaTransactionGetTimestamp (hbarTransaction).seconds;
    bool     hbarSuccess     = !hederaTransactionHasError (hbarTransaction);

    BRCryptoTransferState state =
    (0 != hbarBlockheight
     ? cryptoTransferStateIncludedInit (hbarBlockheight,
                                        0,
                                        hbarTimestamp,
                                        estimatedFeeBasis,
                                        AS_CRYPTO_BOOLEAN(hbarSuccess),
                                        (hbarSuccess ? NULL : "unknown error"))
     : (hbarSuccess
        ? cryptoTransferStateInit (CRYPTO_TRANSFER_STATE_CREATED)
        : cryptoTransferStateErroredInit(cryptoTransferSubmitErrorUnknown())));

    BRCryptoTransfer transfer = cryptoTransferCreateAsHBAR (wallet->listenerTransfer,
                                                            unit,
                                                            unitForFee,
                                                            state,
                                                            walletHBAR->hbarAccount,
                                                            hbarTransaction);

    // Take all the attributes, even if there aren't for HBAR.
    cryptoTransferSetAttributes (transfer, attributesCount, attributes);

    return transfer;
}

extern BRCryptoTransfer
cryptoWalletCreateTransferMultipleHBAR (BRCryptoWallet wallet,
                                        size_t outputsCount,
                                        BRCryptoTransferOutput *outputs,
                                        BRCryptoFeeBasis estimatedFeeBasis,
                                        BRCryptoCurrency currency,
                                        BRCryptoUnit unit,
                                        BRCryptoUnit unitForFee) {
    return NULL;
}

static OwnershipGiven BRSetOf(BRCryptoAddress)
cryptoWalletGetAddressesForRecoveryHBAR (BRCryptoWallet wallet) {
    BRSetOf(BRCryptoAddress) addresses = cryptoAddressSetCreate (1);
    
    BRCryptoWalletHBAR walletHBAR = cryptoWalletCoerce(wallet);

    BRSetAdd (addresses, cryptoAddressCreateAsHBAR (hederaAccountGetAddress(walletHBAR->hbarAccount)));
    
    return addresses;
}

static bool
cryptoWalletIsEqualHBAR (BRCryptoWallet wb1, BRCryptoWallet wb2) {
    if (wb1 == wb2) return true;
    BRCryptoWalletHBAR w1 = cryptoWalletCoerce(wb1);
    BRCryptoWalletHBAR w2 = cryptoWalletCoerce(wb2);
    return w1->hbarAccount == w2->hbarAccount;
}

BRCryptoWalletHandlers cryptoWalletHandlersHBAR = {
    cryptoWalletReleaseHBAR,
    cryptoWalletGetAddressHBAR,
    cryptoWalletHasAddressHBAR,
    cryptoWalletGetTransferAttributeCountHBAR,
    cryptoWalletGetTransferAttributeAtHBAR,
    cryptoWalletValidateTransferAttributeHBAR,
    cryptoWalletCreateTransferHBAR,
    cryptoWalletCreateTransferMultipleHBAR,
    cryptoWalletGetAddressesForRecoveryHBAR,
    NULL,
    cryptoWalletIsEqualHBAR
};
