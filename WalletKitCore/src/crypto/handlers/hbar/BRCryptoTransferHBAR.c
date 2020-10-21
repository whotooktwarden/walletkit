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

static BRHederaTransaction
cryptoTransferCreateTransactionHBAR (BRCryptoTransfer transfer,
                                     const char *txID,
                                     BRHederaTransactionHash *hash) {
    BRHederaAddress sourceAddress = cryptoAddressAsHBAR (transfer->sourceAddress);
    BRHederaAddress targetAddress = cryptoAddressAsHBAR (transfer->targetAddress);

    BRCryptoBoolean overflow;
    BRHederaUnitTinyBar amount = (BRHederaUnitTinyBar) cryptoAmountGetIntegerRaw (transfer->amount, &overflow);

    BRHederaFeeBasis feeBasis = cryptoFeeBasisAsHBAR (transfer->feeBasisEstimated);

    BRCryptoTransferState state = transfer->state;
    uint64_t timestamp   = 0;
    uint64_t blockHeight = 0;
    int error = 0;

    if (CRYPTO_TRANSFER_STATE_INCLUDED == state.type) {
        timestamp = state.u.included.timestamp;
        blockHeight = state.u.included.blockNumber;
        error = CRYPTO_FALSE == state.u.included.success;
    }
    else if (CRYPTO_TRANSFER_STATE_ERRORED == state.type)
        error = 1;

    return hederaTransactionCreate (sourceAddress,
                                    targetAddress,
                                    amount,
                                    feeBasis.pricePerCostFactor,
                                    txID,
                                    *hash,
                                    timestamp,
                                    blockHeight,
                                    error);
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

static BRRlpItem
cryptoTransferRLPEncodeHBAR (BRCryptoTransfer transfer,
                                   BRCryptoNetwork network,
                                   BRRlpCoder coder) {
    BRCryptoTransferHBAR transferHBAR = cryptoTransferCoerceHBAR (transfer);
    BRHederaTransaction transaction = transferHBAR->hbarTransaction;

    BRHederaTransactionHash hash = hederaTransactionGetHash (transaction);
    char *txID = hederaTransactionGetTransactionId (transaction);

    BRRlpItem item =  rlpEncodeList2 (coder,
                                      cryptoTransferRLPEncodeBase (transfer, network, coder),
                                      rlpEncodeList2 (coder,
                                                      rlpEncodeBytes  (coder, hash.bytes, 48),
                                                      rlpEncodeString (coder, txID)));

    if (NULL != txID) free (txID);

    return item;
}

typedef struct {
    BRRlpItem  item;
    BRRlpCoder coder;
} BRCryptoTransferRLPDecodeContext;

static void
cryptoTransferRLPDecodeCreateCallbackHBAR (BRCryptoTransferCreateContext context,
                                           BRCryptoTransfer transfer) {
    BRCryptoTransferRLPDecodeContext *decodeContext = context;
    BRCryptoTransferHBAR transferHBAR = cryptoTransferCoerceHBAR (transfer);

    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (decodeContext->coder,
                                            decodeContext->item,
                                            &itemsCount);
    assert (2 == itemsCount);

    BRRlpData hashData = rlpDecodeBytes (decodeContext->coder, items[0]);
    assert (48 == hashData.bytesCount);
    BRHederaTransactionHash *hash = (BRHederaTransactionHash*) hashData.bytes;

    char *txID = rlpDecodeString (decodeContext->coder, items[1]);

    transferHBAR->hbarTransaction = cryptoTransferCreateTransactionHBAR (transfer, txID, hash);
}

static BRCryptoTransfer
cryptoTransferRLPDecodeHBAR (BRRlpItem item,
                             BRCryptoNetwork network,
                             BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (2 == itemsCount);

    BRCryptoTransferRLPDecodeContext context = { items[1], coder };

    return cryptoTransferRLPDecodeBase (items[0],
                                        network,
                                        &context,
                                        cryptoTransferRLPDecodeCreateCallbackHBAR,
                                        coder);
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
    cryptoTransferRLPEncodeHBAR,
    cryptoTransferRLPDecodeHBAR,
    cryptoTransferIsEqualHBAR
};
