//
//  BRCryptoTransferBTC.c
//  Core
//
//  Created by Ed Gamble on 05/07/2020.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include "BRCryptoBTC.h"
#include "crypto/BRCryptoAmountP.h"
#include "ethereum/util/BRUtilMath.h"

// MARK: - Transfer Create Context

typedef struct {
    BRTransaction *tid;

    bool isDeleted;

    uint64_t fee;
    uint64_t send;
    uint64_t recv;

} BRCryptoTransferCreateContextBTC;

static void
cryptoTransferCreateCallbackBTC (BRCryptoTransferCreateContext context,
                                 BRCryptoTransfer transfer) {
    BRCryptoTransferCreateContextBTC *contextBTC = (BRCryptoTransferCreateContextBTC*) context;
    BRCryptoTransferBTC transferBTC = cryptoTransferCoerceBTC (transfer);

    transferBTC->tid  = contextBTC->tid;

    transferBTC->isDeleted  = contextBTC->isDeleted;

    // cache the values that require the wallet
    transferBTC->fee  = contextBTC->fee;
    transferBTC->recv = contextBTC->recv;
    transferBTC->send = contextBTC->send;
}

static BRRlpItem
cryptoTransferCreateContextRLPEncodeBTC (const BRCryptoTransferCreateContextBTC context,
                                         BRRlpCoder coder) {
    size_t tidBytesSize = BRTransactionSerialize (context.tid, NULL, 0);
    uint8_t tidBytes[tidBytesSize];
    BRTransactionSerialize (context.tid, tidBytes, tidBytesSize);

    return rlpEncodeList (coder, 7,
                          rlpEncodeBytes  (coder, tidBytes, tidBytesSize),
                          rlpEncodeUInt64 (coder, context.tid->blockHeight, 0),
                          rlpEncodeUInt64 (coder, context.tid->timestamp,   0),
                          rlpEncodeUInt64 (coder, context.isDeleted, 0),
                          rlpEncodeUInt64 (coder, context.fee,  0),
                          rlpEncodeUInt64 (coder, context.send, 0),
                          rlpEncodeUInt64 (coder, context.recv, 0));
}

static BRCryptoTransferCreateContextBTC
cryptoTransferCreateContextRLPDecodeBTC (BRRlpItem item,
                                         BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (7 == itemsCount);

    BRRlpData tidData = rlpDecodeBytes (coder, items[0]);
    BRTransaction *tid = BRTransactionParse (tidData.bytes, tidData.bytesCount);
    rlpDataRelease(tidData);

    tid->blockHeight = (uint32_t) rlpDecodeUInt64 (coder, items[1], 0);
    tid->timestamp   = (uint32_t) rlpDecodeUInt64 (coder, items[2], 0);

    return (BRCryptoTransferCreateContextBTC) {
        tid,
        rlpDecodeUInt64 (coder, items[3], 0),
        rlpDecodeUInt64 (coder, items[4], 0),
        rlpDecodeUInt64 (coder, items[5], 0),
        rlpDecodeUInt64 (coder, items[6], 0)
    };
}

// MARK: - Transfer

static BRCryptoTransferDirection
cryptoTransferDirectionFromBTC (uint64_t send, uint64_t recv, uint64_t fee);

static BRCryptoAmount
cryptoTransferAmountFromBTC (BRCryptoTransferDirection direction,
                             BRCryptoUnit unit,
                             uint64_t send,
                             uint64_t recv,
                             uint64_t fee);

extern BRCryptoTransferBTC
cryptoTransferCoerceBTC (BRCryptoTransfer transfer) {
    assert (CRYPTO_NETWORK_TYPE_BTC == transfer->type ||
            CRYPTO_NETWORK_TYPE_BCH == transfer->type ||
            CRYPTO_NETWORK_TYPE_BSV == transfer->type);
    return (BRCryptoTransferBTC) transfer;
}

private_extern OwnershipKept BRTransaction *
cryptoTransferAsBTC (BRCryptoTransfer transfer) {
    BRCryptoTransferBTC transferBTC = cryptoTransferCoerceBTC(transfer);
    return transferBTC->tid;
}

private_extern BRCryptoBoolean
cryptoTransferHasBTC (BRCryptoTransfer transfer,
                      BRTransaction *btc) {
    BRCryptoTransferBTC transferBTC = cryptoTransferCoerceBTC(transfer);
    return AS_CRYPTO_BOOLEAN (BRTransactionEq (btc, transferBTC->tid));
}


extern BRCryptoTransfer
cryptoTransferCreateAsBTC (BRCryptoTransferListener listener,
                           BRCryptoUnit unit,
                           BRCryptoUnit unitForFee,
                           OwnershipKept  BRWallet *wid,
                           OwnershipGiven BRTransaction *tid,
                           BRCryptoBlockChainType type) {
    uint64_t fee  = BRWalletFeeForTx (wid, tid);
    uint64_t recv = BRWalletAmountReceivedFromTx (wid, tid);
    uint64_t send = BRWalletAmountSentByTx (wid, tid);
    
    BRAddressParams  addressParams = BRWalletGetAddressParams (wid);

    BRCryptoTransferDirection direction = cryptoTransferDirectionFromBTC (send, recv, fee);
    
    BRCryptoAmount amount = cryptoTransferAmountFromBTC (direction, unit, send, recv, fee);
    
    BRCryptoAddress sourceAddress = NULL;
    {
        size_t     inputsCount = tid->inCount;
        BRTxInput *inputs      = tid->inputs;

        // If we receive the transfer, then we won't be the source address.
        int inputsContain = (CRYPTO_TRANSFER_RECEIVED != direction);

        for (size_t index = 0; index < inputsCount; index++) {
            size_t addressSize = BRTxInputAddress (&inputs[index], NULL, 0, addressParams);

            // ensure address fits in a BRAddress struct, which adds a nul-terminator
            assert (addressSize < sizeof (BRAddress));
            if (0 != addressSize && addressSize < sizeof (BRAddress)) {
                char address [addressSize + 1];
                BRTxInputAddress (&inputs[index], address, addressSize, addressParams);
                address [addressSize] = '\0'; // ensure address is nul-terminated

                if (inputsContain == BRWalletContainsAddress(wid, address)) {
                    sourceAddress =
                    cryptoAddressCreateAsBTC (type, BRAddressFill (addressParams, address));
                    break;
                }
            }
        }
    }

    BRCryptoAddress targetAddress = NULL;
    {
        size_t      outputsCount = tid->outCount;
        BRTxOutput *outputs      = tid->outputs;

        // If we sent the transfer, then we won't be the target address.
        int outputsContain = (CRYPTO_TRANSFER_SENT != direction);

        for (size_t index = 0; index < outputsCount; index++) {
            size_t addressSize = BRTxOutputAddress (&outputs[index], NULL, 0, addressParams);

            // ensure address fits in a BRAddress struct, which adds a nul-terminator
            assert (addressSize < sizeof (BRAddress));
            if (0 != addressSize && addressSize < sizeof (BRAddress)) {
                // There will be no targetAddress if we send the amount to ourselves.  In that
                // case `outputsContain = 0` and every output is our own address and thus 1 is always
                // returned by `BRWalletContainsAddress()`
                char address [addressSize + 1];
                BRTxOutputAddress (&outputs[index], address, addressSize, addressParams);
                address [addressSize] = '\0'; // ensure address is nul-terminated

                if (outputsContain == BRWalletContainsAddress(wid, address)) {
                    targetAddress =
                    cryptoAddressCreateAsBTC (type, BRAddressFill (addressParams, address));
                    break;
                }
            }
        }
    }


    // Currently this function, cryptoTransferCreateAsBTC(), is only called in various CWM
    // event handlers based on BTC events.  Thus for a newly created BTC transfer, the
    // BRCryptoFeeBasis is long gone.  The best we can do is reconstruct the feeBasis from the
    // BRTransaction itself.

    BRCryptoFeeBasis feeBasisEstimated =
    cryptoFeeBasisCreateAsBTC (type,
                               unitForFee,
                               (fee == UINT64_MAX ? CRYPTO_FEE_BASIS_BTC_FEE_UNKNOWN        : fee),
                               (fee == UINT64_MAX ? 0                                       : CRYPTO_FEE_BASIS_BTC_FEE_PER_KB_UNKNOWN),
                               (uint32_t) BRTransactionVSize (tid));

    BRCryptoTransferCreateContextBTC contextBTC = {
        tid,
        false,
        fee,
        recv,
        send
    };

    BRCryptoTransferState state =
    (TX_UNCONFIRMED != tid->blockHeight
     ? cryptoTransferStateIncludedInit (tid->blockHeight,
                                        0,
                                        tid->timestamp,
                                        feeBasisEstimated,
                                        CRYPTO_TRUE,
                                        NULL)
     : cryptoTransferStateInit (CRYPTO_TRANSFER_STATE_SUBMITTED));

    BRCryptoTransfer transfer = cryptoTransferAllocAndInit (sizeof (struct BRCryptoTransferBTCRecord),
                                                            type,
                                                            listener,
                                                            unit,
                                                            unitForFee,
                                                            feeBasisEstimated,
                                                            amount,
                                                            direction,
                                                            sourceAddress,
                                                            targetAddress,
                                                            state,
                                                            &contextBTC,
                                                            cryptoTransferCreateCallbackBTC);

    cryptoFeeBasisGive (feeBasisEstimated);
    cryptoAmountGive  (amount);
    cryptoAddressGive (sourceAddress);
    cryptoAddressGive (targetAddress);

    return transfer;
}

static void
cryptoTransferReleaseBTC (BRCryptoTransfer transfer) {
    BRCryptoTransferBTC transferBTC = cryptoTransferCoerceBTC(transfer);
    BRTransactionFree (transferBTC->tid);
}

static BRCryptoHash
cryptoTransferGetHashBTC (BRCryptoTransfer transfer) {
    BRCryptoTransferBTC transferBTC = cryptoTransferCoerceBTC(transfer);

    return (1 == UInt256IsZero(transferBTC->tid->txHash)
            ? NULL
            : cryptoHashCreateAsBTC (transferBTC->tid->txHash));
}

extern uint8_t *
cryptoTransferSerializeBTC (BRCryptoTransfer transfer,
                            BRCryptoNetwork  network,
                            BRCryptoBoolean  requireSignature,
                            size_t *serializationCount) {
    assert (CRYPTO_TRUE == requireSignature);
    BRTransaction *tid = cryptoTransferAsBTC     (transfer);

    if (NULL == tid) { *serializationCount = 0; return NULL; }

    *serializationCount = BRTransactionSerialize (tid, NULL, 0);
    uint8_t *serialization = malloc (*serializationCount);

    BRTransactionSerialize (tid, serialization, *serializationCount);
    return serialization;
}

static BRRlpItem
cryptoTransferRLPEncodeBTC (BRCryptoTransfer transfer,
                            BRCryptoNetwork network,
                            BRRlpCoder coder) {
    BRCryptoTransferBTC  transferBTC = cryptoTransferCoerceBTC (transfer);

    BRCryptoTransferCreateContextBTC createContext = {
        transferBTC->tid,
        transferBTC->isDeleted,
        transferBTC->fee,
        transferBTC->send,
        transferBTC->recv
    };

    return rlpEncodeList2 (coder,
                           cryptoTransferRLPEncodeBase (transfer, network, coder),
                           cryptoTransferCreateContextRLPEncodeBTC (createContext, coder));
}

static BRCryptoTransfer
cryptoTransferRLPDecodeBTC (BRRlpItem item,
                            BRCryptoNetwork network,
                            BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (2 == itemsCount);

    BRCryptoTransferCreateContextBTC createContextBTC = cryptoTransferCreateContextRLPDecodeBTC (items[1], coder);

    return cryptoTransferRLPDecodeBase (items[0],
                                        network,
                                        &createContextBTC,
                                        cryptoTransferCreateCallbackBTC,
                                        coder);
}

static int
cryptoTransferIsEqualBTC (BRCryptoTransfer tb1, BRCryptoTransfer tb2) {
    BRCryptoTransferBTC t1 = cryptoTransferCoerceBTC(tb1);
    BRCryptoTransferBTC t2 = cryptoTransferCoerceBTC(tb2);

    // This does not compare the properties of `t1` to `t2`, just the 'id-ness'.  If the properties
    // are compared, one needs to be careful about the BRTransaction's timestamp.  Two transactions
    // with an identical hash can have different timestamps depending on how the transaction
    // is identified.  Specifically P2P and API found transactions *will* have different timestamps.
    return t1 == t2 || BRTransactionEq (t1->tid, t2->tid);
}

static BRCryptoAmount
cryptoTransferAmountFromBTC (BRCryptoTransferDirection direction,
                             BRCryptoUnit unit,
                             uint64_t send,
                             uint64_t recv,
                             uint64_t fee) {
    if (UINT64_MAX == fee) fee = 0;
    
    switch (direction) {
        case CRYPTO_TRANSFER_RECOVERED:
            return cryptoAmountCreate (unit,
                                       CRYPTO_FALSE,
                                       uint256Create(send));
            
        case CRYPTO_TRANSFER_SENT:
            return cryptoAmountCreate (unit,
                                       CRYPTO_FALSE,
                                       uint256Create(send - fee - recv));
            
        case CRYPTO_TRANSFER_RECEIVED:
            return cryptoAmountCreate (unit,
                                       CRYPTO_FALSE,
                                       uint256Create(recv));
            break;
            
        default:
            assert(0);
            return cryptoAmountCreate (unit,
                                       CRYPTO_FALSE,
                                       UINT256_ZERO);
    }
}

static BRCryptoTransferDirection
cryptoTransferDirectionFromBTC (uint64_t send, uint64_t recv, uint64_t fee) {
    if (UINT64_MAX == fee) fee = 0;

    return (0 == send
            ? CRYPTO_TRANSFER_RECEIVED
            : ((send - fee) == recv
               ? CRYPTO_TRANSFER_RECOVERED
               : ((send - fee) > recv
                  ? CRYPTO_TRANSFER_SENT
                  : CRYPTO_TRANSFER_RECEIVED)));
}


BRCryptoTransferHandlers cryptoTransferHandlersBTC = {
    cryptoTransferReleaseBTC,
    cryptoTransferGetHashBTC,
    cryptoTransferSerializeBTC,
    NULL, // getBytesForFeeEstimate
    cryptoTransferRLPEncodeBTC,
    cryptoTransferRLPDecodeBTC,
    cryptoTransferIsEqualBTC
};

BRCryptoTransferHandlers cryptoTransferHandlersBCH = {
    cryptoTransferReleaseBTC,
    cryptoTransferGetHashBTC,
    cryptoTransferSerializeBTC,
    NULL, // getBytesForFeeEstimate
    cryptoTransferRLPEncodeBTC,
    cryptoTransferRLPDecodeBTC,
    cryptoTransferIsEqualBTC
};

BRCryptoTransferHandlers cryptoTransferHandlersBSV = {
    cryptoTransferReleaseBTC,
    cryptoTransferGetHashBTC,
    cryptoTransferSerializeBTC,
    NULL, // getBytesForFeeEstimate
    cryptoTransferRLPEncodeBTC,
    cryptoTransferRLPDecodeBTC,
    cryptoTransferIsEqualBTC
};
