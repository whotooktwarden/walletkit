//
//  BRCryptoTransferHBAR.c
//  Core
//
//  Created by Ehsan Rezaie on 2020-05-19.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include "BRCryptoHBAR.h"
#include "crypto/BRCryptoAmountP.h"
#include "crypto/BRCryptoHashP.h"
#include "hedera/BRHederaTransaction.h"
#include "ethereum/util/BRUtilMath.h"

static BRCryptoTransferDirection
transferGetDirectionFromHBAR (BRHederaTransaction transaction,
                              BRHederaAccount account);

extern BRCryptoTransferHBAR
cryptoTransferCoerceHBAR (BRCryptoTransfer transfer) {
    assert (CRYPTO_NETWORK_TYPE_HBAR == transfer->type);
    return (BRCryptoTransferHBAR) transfer;
}

typedef struct {
    BRHederaTransaction hbarTransaction;
} BRCryptoTransferCreateContextHBAR;

static void
cryptoTransferCreateCallbackHBAR (BRCryptoTransferCreateContext context,
                                    BRCryptoTransfer transfer) {
    BRCryptoTransferCreateContextHBAR *contextHBAR = (BRCryptoTransferCreateContextHBAR*) context;
    BRCryptoTransferHBAR transferHBAR = cryptoTransferCoerceHBAR (transfer);

    transferHBAR->hbarTransaction = contextHBAR->hbarTransaction;
}

extern BRCryptoTransfer
cryptoTransferCreateAsHBAR (BRCryptoTransferListener listener,
                            BRCryptoUnit unit,
                            BRCryptoUnit unitForFee,
                            BRCryptoTransferState state,
                            OwnershipKept BRHederaAccount hbarAccount,
                            OwnershipGiven BRHederaTransaction hbarTransaction) {
    
    BRCryptoTransferDirection direction = transferGetDirectionFromHBAR (hbarTransaction, hbarAccount);
    
    BRCryptoAmount amount = cryptoAmountCreateAsHBAR (unit,
                                                      CRYPTO_FALSE,
                                                      hederaTransactionGetAmount (hbarTransaction));
    
    BRHederaFeeBasis hbarFeeBasis = { hederaTransactionGetFee (hbarTransaction), 1 };
    BRCryptoFeeBasis feeBasisEstimated = cryptoFeeBasisCreateAsHBAR (unitForFee, hbarFeeBasis);
    
    BRCryptoAddress sourceAddress = cryptoAddressCreateAsHBAR (hederaTransactionGetSource (hbarTransaction));
    BRCryptoAddress targetAddress = cryptoAddressCreateAsHBAR (hederaTransactionGetTarget (hbarTransaction));

    BRCryptoTransferCreateContextHBAR contextHBAR = {
        hbarTransaction
    };

    BRCryptoTransfer transfer = cryptoTransferAllocAndInit (sizeof (struct BRCryptoTransferHBARRecord),
                                                            CRYPTO_NETWORK_TYPE_HBAR,
                                                            listener,
                                                            unit,
                                                            unitForFee,
                                                            feeBasisEstimated,
                                                            amount,
                                                            direction,
                                                            sourceAddress,
                                                            targetAddress,
                                                            state,
                                                            &contextHBAR,
                                                            cryptoTransferCreateCallbackHBAR);

    cryptoFeeBasisGive (feeBasisEstimated);
    cryptoAddressGive (sourceAddress);
    cryptoAddressGive (targetAddress);
    
    return transfer;
}

static void
cryptoTransferReleaseHBAR (BRCryptoTransfer transfer) {
    BRCryptoTransferHBAR transferHBAR = cryptoTransferCoerceHBAR(transfer);
    hederaTransactionFree (transferHBAR->hbarTransaction);
}

static BRCryptoHash
cryptoTransferGetHashHBAR (BRCryptoTransfer transfer) {
    BRCryptoTransferHBAR transferHBAR = cryptoTransferCoerceHBAR(transfer);
    BRHederaTransactionHash hash = hederaTransactionGetHash (transferHBAR->hbarTransaction);
    return cryptoHashCreateAsHBAR (hash);
}

static uint8_t *
cryptoTransferSerializeHBAR (BRCryptoTransfer transfer,
                             BRCryptoNetwork network,
                             BRCryptoBoolean  requireSignature,
                             size_t *serializationCount) {
    assert (CRYPTO_TRUE == requireSignature);
    BRCryptoTransferHBAR transferHBAR = cryptoTransferCoerceHBAR (transfer);
    return hederaTransactionSerialize (transferHBAR->hbarTransaction, serializationCount);
}

static int
cryptoTransferIsEqualHBAR (BRCryptoTransfer t1, BRCryptoTransfer t2) {
    if (t1 == t2) return 1;

    BRCryptoTransferHBAR th1 = cryptoTransferCoerceHBAR (t1);
    BRCryptoTransferHBAR th2 = cryptoTransferCoerceHBAR (t2);

    if (th1->hbarTransaction == th2->hbarTransaction) return 1;

    return hederaTransactionHashIsEqual (hederaTransactionGetHash (th1->hbarTransaction),
                                         hederaTransactionGetHash (th2->hbarTransaction));
}

static BRCryptoTransferDirection
transferGetDirectionFromHBAR (BRHederaTransaction transaction,
                              BRHederaAccount account) {
    BRHederaAddress source = hederaTransactionGetSource (transaction);
    BRHederaAddress target = hederaTransactionGetTarget (transaction);
    
    int isSource = hederaAccountHasAddress (account, source);
    int isTarget = hederaAccountHasAddress (account, target);

    hederaAddressFree (target);
    hederaAddressFree (source);
    
    return (isSource && isTarget
            ? CRYPTO_TRANSFER_RECOVERED
            : (isSource
               ? CRYPTO_TRANSFER_SENT
               : CRYPTO_TRANSFER_RECEIVED));
}

BRCryptoTransferHandlers cryptoTransferHandlersHBAR = {
    cryptoTransferReleaseHBAR,
    cryptoTransferGetHashHBAR,
    cryptoTransferSerializeHBAR,
    NULL, // getBytesForFeeEstimate
    cryptoTransferIsEqualHBAR
};
