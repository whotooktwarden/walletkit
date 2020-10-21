//
//  BRCryptoTransferXTZ.c
//  Core
//
//  Created by Ehsan Rezaie on 2020-08-27.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include "BRCryptoXTZ.h"
#include "crypto/BRCryptoAmountP.h"
#include "crypto/BRCryptoHashP.h"
#include "tezos/BRTezosTransfer.h"
#include "tezos/BRTezosFeeBasis.h"
#include "ethereum/util/BRUtilMath.h"

static BRCryptoTransferDirection
transferGetDirectionFromXTZ (BRTezosTransfer transfer,
                             BRTezosAccount account);

extern BRCryptoTransferXTZ
cryptoTransferCoerceXTZ (BRCryptoTransfer transfer) {
    assert (CRYPTO_NETWORK_TYPE_XTZ == transfer->type);
    return (BRCryptoTransferXTZ) transfer;
}

typedef struct {
    BRTezosTransfer xtzTransfer;
} BRCryptoTransferCreateContextXTZ;

static void
cryptoTransferCreateCallbackXTZ (BRCryptoTransferCreateContext context,
                                    BRCryptoTransfer transfer) {
    BRCryptoTransferCreateContextXTZ *contextXTZ = (BRCryptoTransferCreateContextXTZ*) context;
    BRCryptoTransferXTZ transferXTZ = cryptoTransferCoerceXTZ (transfer);

    transferXTZ->xtzTransfer = contextXTZ->xtzTransfer;
}

extern BRCryptoTransfer
cryptoTransferCreateAsXTZ (BRCryptoTransferListener listener,
                           BRCryptoUnit unit,
                           BRCryptoUnit unitForFee,
                           BRCryptoTransferState state,
                           OwnershipKept BRTezosAccount xtzAccount,
                           OwnershipGiven BRTezosTransfer xtzTransfer) {
    
    BRCryptoTransferDirection direction = transferGetDirectionFromXTZ (xtzTransfer, xtzAccount);
    
    BRCryptoAmount amount = cryptoAmountCreateAsXTZ (unit,
                                                     CRYPTO_FALSE,
                                                     tezosTransferGetAmount (xtzTransfer));
    
    BRTezosFeeBasis xtzFeeBasis = tezosFeeBasisCreateActual (tezosTransferGetFee(xtzTransfer));
    BRCryptoFeeBasis feeBasis = cryptoFeeBasisCreateAsXTZ (unitForFee,
                                                           xtzFeeBasis);
    
    BRCryptoAddress sourceAddress = cryptoAddressCreateAsXTZ (tezosTransferGetSource (xtzTransfer));
    BRCryptoAddress targetAddress = cryptoAddressCreateAsXTZ (tezosTransferGetTarget (xtzTransfer));

    BRCryptoTransferCreateContextXTZ contextXTZ = {
        xtzTransfer
    };

    BRCryptoTransfer transfer = cryptoTransferAllocAndInit (sizeof (struct BRCryptoTransferXTZRecord),
                                                            CRYPTO_NETWORK_TYPE_XTZ,
                                                            listener,
                                                            unit,
                                                            unitForFee,
                                                            feeBasis,
                                                            amount,
                                                            direction,
                                                            sourceAddress,
                                                            targetAddress,
                                                            state,
                                                            &contextXTZ,
                                                            cryptoTransferCreateCallbackXTZ);
    
    cryptoFeeBasisGive (feeBasis);
    cryptoAddressGive (sourceAddress);
    cryptoAddressGive (targetAddress);

    return transfer;
}

static void
cryptoTransferReleaseXTZ (BRCryptoTransfer transfer) {
    BRCryptoTransferXTZ transferXTZ = cryptoTransferCoerceXTZ(transfer);
    tezosTransferFree (transferXTZ->xtzTransfer);
}

static BRCryptoHash
cryptoTransferGetHashXTZ (BRCryptoTransfer transfer) {
    BRCryptoTransferXTZ transferXTZ = cryptoTransferCoerceXTZ(transfer);
    BRTezosHash hash = tezosTransferGetTransactionId (transferXTZ->xtzTransfer);
    return cryptoHashCreateAsXTZ (hash);
}

static BRTezosTransfer
cryptoTransferCreateTransferXTZ (BRCryptoTransfer transfer,
                                 BRTezosHash *hash) {
    BRTezosAddress sourceAddress = cryptoAddressAsXTZ (transfer->sourceAddress);
    BRTezosAddress targetAddress = cryptoAddressAsXTZ (transfer->targetAddress);

    BRCryptoBoolean overflow;
    BRTezosUnitMutez amount = (BRTezosUnitMutez) cryptoAmountGetIntegerRaw (transfer->amount, &overflow);

    BRTezosFeeBasis feeBasis = cryptoFeeBasisAsXTZ (transfer->feeBasisEstimated);

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

    return tezosTransferCreate (sourceAddress,
                                targetAddress,
                                amount,
                                tezosFeeBasisGetFee(&feeBasis),
                                *hash,
                                timestamp,
                                blockHeight,
                                error);
}

static uint8_t *
cryptoTransferSerializeXTZ (BRCryptoTransfer transfer,
                            BRCryptoNetwork network,
                            BRCryptoBoolean  requireSignature,
                            size_t *serializationCount) {
    BRCryptoTransferXTZ transferXTZ = cryptoTransferCoerceXTZ (transfer);

    uint8_t *serialization = NULL;
    *serializationCount = 0;
    BRTezosTransaction transaction = tezosTransferGetTransaction (transferXTZ->xtzTransfer);
    if (transaction) {
        serialization = tezosTransactionGetSignedBytes (transaction, serializationCount);
    }
    
    return serialization;
}

static BRRlpItem
cryptoTransferRLPEncodeXTZ (BRCryptoTransfer transfer,
                            BRCryptoNetwork network,
                            BRRlpCoder coder) {
    BRCryptoTransferXTZ transferXTZ = cryptoTransferCoerceXTZ (transfer);
    BRTezosTransfer xtzTransfer = transferXTZ->xtzTransfer;

    BRTezosHash hash = tezosTransferGetTransactionId (xtzTransfer);

    return rlpEncodeList2 (coder,
                           cryptoTransferRLPEncodeBase (transfer, network, coder),
                           rlpEncodeBytes (coder, hash.bytes, TEZOS_HASH_BYTES));
}

typedef struct {
    BRRlpItem  item;
    BRRlpCoder coder;
} BRCryptoTransferRLPDecodeContext;

static void
cryptoTransferRLPDecodeCreateCallbackXTZ (BRCryptoTransferCreateContext context,
                                          BRCryptoTransfer transfer) {
    BRCryptoTransferRLPDecodeContext *decodeContext = context;
    BRCryptoTransferXTZ transferXTZ = cryptoTransferCoerceXTZ (transfer);

    BRRlpData hashData = rlpDecodeBytes (decodeContext->coder, decodeContext->item);
    assert (TEZOS_HASH_BYTES == hashData.bytesCount);
    BRTezosHash *hash = (BRTezosHash*) hashData.bytes;

    transferXTZ->xtzTransfer = cryptoTransferCreateTransferXTZ (transfer, hash);
}

static BRCryptoTransfer
cryptoTransferRLPDecodeXTZ (BRRlpItem item,
                            BRCryptoNetwork network,
                            BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (2 == itemsCount);

    BRCryptoTransferRLPDecodeContext context = { items[1], coder };

    return cryptoTransferRLPDecodeBase (items[0],
                                        network,
                                        &context,
                                        cryptoTransferRLPDecodeCreateCallbackXTZ,
                                        coder);
}

static int
cryptoTransferIsEqualXTZ (BRCryptoTransfer tb1, BRCryptoTransfer tb2) {
    if (tb1 == tb2) return 1;
    
    BRCryptoTransferXTZ tz1 = cryptoTransferCoerceXTZ (tb1);
    BRCryptoTransferXTZ tz2 = cryptoTransferCoerceXTZ (tb2);
    
    return tezosTransferIsEqual (tz1->xtzTransfer, tz2->xtzTransfer);
}

static BRCryptoTransferDirection
transferGetDirectionFromXTZ (BRTezosTransfer transfer,
                             BRTezosAccount account) {
    BRTezosAddress source = tezosTransferGetSource (transfer);
    BRTezosAddress target = tezosTransferGetTarget (transfer);
    
    int isSource = tezosAccountHasAddress (account, source);
    int isTarget = tezosAccountHasAddress (account, target);
    
    tezosAddressFree (target);
    tezosAddressFree (source);
    
    return (isSource && isTarget
            ? CRYPTO_TRANSFER_RECOVERED
            : (isSource
               ? CRYPTO_TRANSFER_SENT
               : CRYPTO_TRANSFER_RECEIVED));
}

BRCryptoTransferHandlers cryptoTransferHandlersXTZ = {
    cryptoTransferReleaseXTZ,
    cryptoTransferGetHashXTZ,
    cryptoTransferSerializeXTZ,
    NULL, // getBytesForFeeEstimate
    cryptoTransferRLPEncodeXTZ,
    cryptoTransferRLPDecodeXTZ,
    cryptoTransferIsEqualXTZ
};
