//
//  BRCryptoTransferXRP.c
//  Core
//
//  Created by Ehsan Rezaie on 2020-05-19.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include "BRCryptoXRP.h"
#include "crypto/BRCryptoAmountP.h"
#include "crypto/BRCryptoHashP.h"
#include "ripple/BRRippleTransaction.h"
#include "ethereum/util/BRUtilMath.h"

static BRCryptoTransferDirection
transferGetDirectionFromXRP (BRRippleTransaction transaction,
                             BRRippleAccount account);

extern BRCryptoTransferXRP
cryptoTransferCoerceXRP (BRCryptoTransfer transfer) {
    assert (CRYPTO_NETWORK_TYPE_XRP == transfer->type);
    return (BRCryptoTransferXRP) transfer;
}

typedef struct {
    BRRippleTransaction xrpTransaction;
} BRCryptoTransferCreateContextXRP;

extern BRRippleTransaction
cryptoTransferAsXRP (BRCryptoTransfer transfer) {
    BRCryptoTransferXRP transferXRP = cryptoTransferCoerceXRP (transfer);
    return transferXRP->xrpTransaction;
}

static void
cryptoTransferCreateCallbackXRP (BRCryptoTransferCreateContext context,
                                    BRCryptoTransfer transfer) {
    BRCryptoTransferCreateContextXRP *contextXRP = (BRCryptoTransferCreateContextXRP*) context;
    BRCryptoTransferXRP transferXRP = cryptoTransferCoerceXRP (transfer);

    transferXRP->xrpTransaction = contextXRP->xrpTransaction;
}

extern BRCryptoTransfer
cryptoTransferCreateAsXRP (BRCryptoTransferListener listener,
                           BRCryptoUnit unit,
                           BRCryptoUnit unitForFee,
                           BRCryptoTransferState state,
                           OwnershipKept BRRippleAccount xrpAccount,
                           OwnershipGiven BRRippleTransaction xrpTransfer) {
    
    BRCryptoTransferDirection direction = transferGetDirectionFromXRP (xrpTransfer, xrpAccount);
    
    BRCryptoAmount amount = cryptoAmountCreateAsXRP (unit,
                                                     CRYPTO_FALSE,
                                                     rippleTransactionGetAmount(xrpTransfer));

    BRCryptoFeeBasis feeBasisEstimated = cryptoFeeBasisCreateAsXRP (unitForFee, rippleTransactionGetFee(xrpTransfer));
    
    BRCryptoAddress sourceAddress = cryptoAddressCreateAsXRP (rippleTransactionGetSource(xrpTransfer));
    BRCryptoAddress targetAddress = cryptoAddressCreateAsXRP (rippleTransactionGetTarget(xrpTransfer));

    BRCryptoTransferCreateContextXRP contextXRP = {
        xrpTransfer
    };

    BRCryptoTransfer transfer = cryptoTransferAllocAndInit (sizeof (struct BRCryptoTransferXRPRecord),
                                                            CRYPTO_NETWORK_TYPE_XRP,
                                                            listener,
                                                            unit,
                                                            unitForFee,
                                                            feeBasisEstimated,
                                                            amount,
                                                            direction,
                                                            sourceAddress,
                                                            targetAddress,
                                                            state,
                                                            &contextXRP,
                                                            cryptoTransferCreateCallbackXRP);
    
    cryptoFeeBasisGive (feeBasisEstimated);
    cryptoAddressGive (sourceAddress);
    cryptoAddressGive (targetAddress);

    return transfer;
}

static void
cryptoTransferReleaseXRP (BRCryptoTransfer transfer) {
    BRCryptoTransferXRP transferXRP = cryptoTransferCoerceXRP(transfer);
    rippleTransactionFree (transferXRP->xrpTransaction);
}

static BRCryptoHash
cryptoTransferGetHashXRP (BRCryptoTransfer transfer) {
    BRCryptoTransferXRP transferXRP = cryptoTransferCoerceXRP(transfer);
    BRRippleTransactionHash hash = rippleTransactionGetHash(transferXRP->xrpTransaction);
    return cryptoHashCreateAsXRP (hash);
}

static BRRippleTransaction
cryptoTransferCreateTransactionXRP (BRCryptoTransfer transfer,
                                    BRRippleTransactionHash *hash) {
    BRRippleAddress sourceAddress = cryptoAddressAsXRP (transfer->sourceAddress);
    BRRippleAddress targetAddress = cryptoAddressAsXRP (transfer->targetAddress);

    BRCryptoBoolean overflow;
    BRRippleUnitDrops amount = (BRRippleUnitDrops) cryptoAmountGetIntegerRaw (transfer->amount, &overflow);

    BRRippleFeeBasis feeBasis = cryptoFeeBasisAsXRP (transfer->feeBasisEstimated);

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

    return rippleTransactionCreateFull (sourceAddress,
                                        targetAddress,
                                        amount,
                                        feeBasis,
                                        *hash,
                                        timestamp,
                                        blockHeight,
                                        error);
}

static uint8_t *
cryptoTransferSerializeXRP (BRCryptoTransfer transfer,
                            BRCryptoNetwork network,
                            BRCryptoBoolean  requireSignature,
                            size_t *serializationCount) {
    assert (CRYPTO_TRUE == requireSignature);
    BRCryptoTransferXRP transferXRP = cryptoTransferCoerceXRP (transfer);

    uint8_t *serialization = NULL;
    *serializationCount = 0;
    BRRippleTransaction transaction = transferXRP->xrpTransaction;
    if (transaction) {
        serialization = rippleTransactionSerialize (transaction, serializationCount);
    }

    return serialization;
}

static BRRlpItem
cryptoTransferRLPEncodeXRP (BRCryptoTransfer transfer,
                            BRCryptoNetwork network,
                            BRRlpCoder coder) {
    BRCryptoTransferXRP transferXRP = cryptoTransferCoerceXRP (transfer);
    BRRippleTransaction transaction = transferXRP->xrpTransaction;

    BRRippleTransactionHash hash = rippleTransactionGetHash (transaction);

    return rlpEncodeList2 (coder,
                           cryptoTransferRLPEncodeBase (transfer, network, coder),
                           rlpEncodeBytes (coder, hash.bytes, 32));
}

typedef struct {
    BRRlpItem  item;
    BRRlpCoder coder;
} BRCryptoTransferRLPDecodeContext;

static void
cryptoTransferRLPDecodeCreateCallbackXRP (BRCryptoTransferCreateContext context,
                                          BRCryptoTransfer transfer) {
    BRCryptoTransferRLPDecodeContext *decodeContext = context;
    BRCryptoTransferXRP transferXRP = cryptoTransferCoerceXRP (transfer);

    BRRlpData hashData = rlpDecodeBytes (decodeContext->coder, decodeContext->item);
    assert (32 == hashData.bytesCount);
    BRRippleTransactionHash *hash = (BRRippleTransactionHash*) hashData.bytes;

    transferXRP->xrpTransaction = cryptoTransferCreateTransactionXRP (transfer, hash);
}

static BRCryptoTransfer
cryptoTransferRLPDecodeXRP (BRRlpItem item,
                            BRCryptoNetwork network,
                            BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (2 == itemsCount);
    
    BRCryptoTransferRLPDecodeContext context = { items[1], coder };

    return cryptoTransferRLPDecodeBase (items[0],
                                        network,
                                        &context,
                                        cryptoTransferRLPDecodeCreateCallbackXRP,
                                        coder);
}

static int
cryptoTransferIsEqualXRP (BRCryptoTransfer tb1, BRCryptoTransfer tb2) {
    if (tb1 == tb2) return 1;

    BRCryptoHash h1 = cryptoTransferGetHashXRP (tb1);
    BRCryptoHash h2 = cryptoTransferGetHashXRP (tb2);

    int result = (CRYPTO_TRUE == cryptoHashEqual (h1, h2));

    cryptoHashGive (h2);
    cryptoHashGive (h1);

    return result;
}

static BRCryptoTransferDirection
transferGetDirectionFromXRP (BRRippleTransaction transaction,
                             BRRippleAccount account) {
    BRRippleAddress address = rippleAccountGetAddress(account);

    int isSource = rippleTransactionHasSource(transaction, address);
    int isTarget = rippleTransactionHasTarget(transaction, address);

    rippleAddressFree(address);

    return (isSource && isTarget
            ? CRYPTO_TRANSFER_RECOVERED
            : (isSource
               ? CRYPTO_TRANSFER_SENT
               : CRYPTO_TRANSFER_RECEIVED));
}

BRCryptoTransferHandlers cryptoTransferHandlersXRP = {
    cryptoTransferReleaseXRP,
    cryptoTransferGetHashXRP,
    cryptoTransferSerializeXRP,
    NULL, // getBytesForFeeEstimate
    cryptoTransferRLPEncodeXRP,
    cryptoTransferRLPDecodeXRP,
    cryptoTransferIsEqualXRP
};
