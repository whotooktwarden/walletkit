//
//  BRCryptoTransferETH.c
//  Core
//
//  Created by Ed Gamble on 05/07/2020.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include "BRCryptoETH.h"
#include "support/BRInt.h"
#include "crypto/BRCryptoAmountP.h"

#include "ethereum/blockchain/BREthereumTransaction.h"
#include "ethereum/blockchain/BREthereumLog.h"
#include "ethereum/contract/BREthereumExchange.h"

static BRCryptoTransferDirection
cryptoTransferFindDirection (BREthereumAccount account,
                             BREthereumAddress source,
                             BREthereumAddress target);

extern BRCryptoTransferETH
cryptoTransferCoerceETH (BRCryptoTransfer transfer) {
    assert (CRYPTO_NETWORK_TYPE_ETH == transfer->type);
    return (BRCryptoTransferETH) transfer;
}

#if 0
private_extern BRCryptoTransfer
cryptoTransferCreateAsETHX (BRCryptoUnit unit,
                           BRCryptoUnit unitForFee,
//                           BREthereumEWM ewm,
//                           BREthereumTransfer tid,
                           BRCryptoFeeBasis feeBasisEstimated) {
    BRCryptoTransfer transfer = cryptoTransferAllocAndInit (sizeof (struct BRCryptoTransferETHRecord),
                                                                CRYPTO_NETWORK_TYPE_ETH,
                                                                unit,
                                                                unitForFee);
    BRCryptoTransferETH transferETH = cryptoTransferCoerceETH (transfer);

//    transfer->ewm = ewm;
//    transfer->tid = tid;
//
//    transfer->sourceAddress = cryptoAddressCreateAsETH (ewmTransferGetSource (ewm, tid));
//    transfer->targetAddress = cryptoAddressCreateAsETH (ewmTransferGetTarget (ewm, tid));

    // cache the values that require the ewm
    BREthereumAccount account = ewmGetAccount (ewm);
    transfer->accountAddress = ethAccountGetPrimaryAddress (account);

    // This function `cryptoTransferCreateAsETH()` includes an argument as
    // `BRCryptoFeeBasis feeBasisEstimated` whereas the analogous function
    // `cryptoTransferCreateAsBTC` does not.  Why is that?  For BTC the fee basis can be derived
    // 100% reliably from the BRTransaction; both the 'estimated' and 'confirmed' fee basises are
    // identical.  For ETH, the 'estimated' and the 'confirmed' basises may differ.  The difference
    // being the distinction between ETH `gasLimit` (the 'estimate') and `gasUsed` (the
    // 'confirmed').
    //
    // The EWM interface does not make this distinction clear.  It should.
    // TODO: In EWM expose 'getEstimatedFeeBasis' and 'getConfirmedFeeBasis' functions.
    //
    // Turns out that this function is called in two contexts - when Crypto creates a transfer (in
    // response to User input) and when EWM has a transfer announced (like when found in a
    // blockchain).  When Crypto creates the transfer we have the `feeBasisEstimated` and it is used
    // to create the EWM transfer.  Then EWM finds the transfer (see `cwmTransactionEventAsETH()`)
    // we don't have the estimated fee - if we did nothing the `transfer->feeBasisEstimated` field
    // would be NULL.
    //
    // Problem is we *require* one of 'estimated' or 'confirmed'.  See Transfer.swift at
    // `public var fee: Amount { ... guard let feeBasis = confirmedFeeBasis ?? estimatedFeeBasis }`
    // The 'confirmed' value is *ONLY SET* when a transfer is actually included in the blockchain;
    // therefore we need an estimated fee basis.
    //
    // Thus: if `feeBasisEstimated` is NULL, we'll take the ETH fee basis (as the best we have).

    // Get the ETH feeBasis, in the event that we need it.
    BREthereumFeeBasis ethFeeBasis = ewmTransferGetFeeBasis (ewm, tid);

    transfer->feeBasisEstimated = (NULL == feeBasisEstimated
                                   ? cryptoFeeBasisCreateAsETH (unitForFee,
                                                                ethFeeBasis.u.gas.limit,
                                                                ethFeeBasis.u.gas.price)
                                   : cryptoFeeBasisTake(feeBasisEstimated));

    return transfer;
}
#endif

extern BRCryptoTransferState
cryptoTransferDeriveStateETH (BREthereumTransactionStatus status,
                              BRCryptoFeeBasis feeBasis) {
    switch (status.type) {
        case TRANSACTION_STATUS_UNKNOWN:
            return cryptoTransferStateInit (CRYPTO_TRANSFER_STATE_CREATED);
        case TRANSACTION_STATUS_QUEUED:
        case TRANSACTION_STATUS_PENDING:
            return cryptoTransferStateInit (CRYPTO_TRANSFER_STATE_SUBMITTED);

        case TRANSACTION_STATUS_INCLUDED:
            return cryptoTransferStateIncludedInit (status.u.included.blockNumber,
                                                    status.u.included.transactionIndex,
                                                    status.u.included.blockTimestamp,
                                                    cryptoFeeBasisTake (feeBasis),
                                                    CRYPTO_TRUE,
                                                    NULL);
            break;

        case TRANSACTION_STATUS_ERRORED:
            return cryptoTransferStateErroredInit((BRCryptoTransferSubmitError) {
                CRYPTO_TRANSFER_SUBMIT_ERROR_UNKNOWN
            });
    }
}

typedef struct {
    BREthereumAccount account;
    BREthereumTransferBasis basis;
    BREthereumTransaction originatingTransaction;
} BRCryptoTransferCreateContextETH;

static void
cryptoTransferCreateCallbackETH (BRCryptoTransferCreateContext context,
                                 BRCryptoTransfer transfer) {
    BRCryptoTransferCreateContextETH *contextETH = (BRCryptoTransferCreateContextETH*) context;
    BRCryptoTransferETH transferETH = cryptoTransferCoerceETH (transfer);

    transferETH->account = contextETH->account;
    transferETH->basis   = contextETH->basis;
    transferETH->originatingTransaction = contextETH->originatingTransaction;
}

extern BRCryptoTransfer
cryptoTransferCreateAsETH (BRCryptoTransferListener listener,
                           BRCryptoUnit unit,
                           BRCryptoUnit unitForFee,
                           BRCryptoFeeBasis feeBasisEstimated,
                           BRCryptoAmount amount,
                           BRCryptoTransferDirection direction,
                           BRCryptoAddress sourceAddress,
                           BRCryptoAddress targetAddress,
                           BRCryptoTransferState transferState,
                           BREthereumAccount account,
                           BREthereumTransferBasis basis,
                           OwnershipGiven BREthereumTransaction originatingTransaction) {
    BRCryptoTransferCreateContextETH contextETH = {
        account,
        basis,
        originatingTransaction
    };

    BRCryptoTransfer transfer = cryptoTransferAllocAndInit (sizeof (struct BRCryptoTransferETHRecord),
                                                            CRYPTO_NETWORK_TYPE_ETH,
                                                            listener,
                                                            unit,
                                                            unitForFee,
                                                            feeBasisEstimated,
                                                            amount,
                                                            direction,
                                                            sourceAddress,
                                                            targetAddress,
                                                            transferState,
                                                            &contextETH,
                                                            cryptoTransferCreateCallbackETH);

    // nothing

    return transfer;
}

extern BRCryptoTransfer
cryptoTransferCreateWithTransactionAsETH (BRCryptoTransferListener listener,
                                          BRCryptoUnit unit,
                                          BRCryptoUnit unitForFee,
                                          BREthereumAccount account,
                                          OwnershipGiven BREthereumTransaction ethTransaction) {

    BRCryptoTransferDirection direction = cryptoTransferFindDirection (account,
                                                                       transactionGetSourceAddress (ethTransaction),
                                                                       transactionGetTargetAddress (ethTransaction));
    BREthereumEther ethAmount = transactionGetAmount(ethTransaction);
    BRCryptoAmount  amount    = cryptoAmountCreate (unit, CRYPTO_FALSE, ethEtherGetValue (ethAmount, WEI));

    // Get the estimated and confirmed feeBasis'.  If ethTransaction is not INCLUDDED, then the
    // confirmedFeeBasis will be the estimate.
    BRCryptoFeeBasis estimatedFeeBasis = cryptoFeeBasisCreateAsETH (unitForFee, transactionGetFeeBasisEstimated (ethTransaction));
    BRCryptoFeeBasis confirmedFeeBasis = cryptoFeeBasisCreateAsETH (unitForFee, transactionGetFeeBasis(ethTransaction));

    BRCryptoAddress  source = cryptoAddressCreateAsETH (transactionGetSourceAddress (ethTransaction));
    BRCryptoAddress  target = cryptoAddressCreateAsETH (transactionGetTargetAddress (ethTransaction));

    BRCryptoTransferState transferState = cryptoTransferDeriveStateETH (transactionGetStatus(ethTransaction),
                                                                        confirmedFeeBasis);

    BREthereumTransferBasis basis = {
        TRANSFER_BASIS_TRANSACTION,
        { .transaction = ethTransaction }
    };

    BRCryptoTransfer transfer = cryptoTransferCreateAsETH (listener,
                                                           unit,
                                                           unitForFee,
                                                           estimatedFeeBasis,
                                                           amount,
                                                           direction,
                                                           source,
                                                           target,
                                                           transferState,
                                                           account,
                                                           basis,
                                                           NULL);

    cryptoFeeBasisGive(confirmedFeeBasis);
    cryptoFeeBasisGive(estimatedFeeBasis);
    cryptoAddressGive(source);
    cryptoAddressGive(target);

    return transfer;
}

extern BRCryptoTransfer
cryptoTransferCreateWithLogAsETH (BRCryptoTransferListener listener,
                                  BRCryptoUnit unit,
                                  BRCryptoUnit unitForFee,
                                  BREthereumAccount account,
                                  UInt256 ethAmount,
                                  OwnershipGiven BREthereumLog ethLog) {
    BREthereumAddress ethSource = logTopicAsAddress(logGetTopic(ethLog, 1));
    BREthereumAddress ethTarget = logTopicAsAddress(logGetTopic(ethLog, 2));

    BRCryptoTransferDirection direction = cryptoTransferFindDirection (account, ethSource, ethTarget);

    BRCryptoAmount  amount    = cryptoAmountCreate (unit, CRYPTO_FALSE, ethAmount);

    BREthereumFeeBasis ethFeeBasis = ethFeeBasisCreate (ethGasCreate(0),
                                                        ethGasPriceCreate(ethEtherCreateZero()));
    BRCryptoFeeBasis estimatedFeeBasis = cryptoFeeBasisCreateAsETH(unitForFee, ethFeeBasis);

    BRCryptoAddress  source = cryptoAddressCreateAsETH (ethSource);
    BRCryptoAddress  target = cryptoAddressCreateAsETH (ethTarget);

    BRCryptoTransferState transferState = cryptoTransferDeriveStateETH (logGetStatus(ethLog),
                                                                        estimatedFeeBasis);

    BREthereumTransferBasis basis = {
        TRANSFER_BASIS_LOG,
        { .log = ethLog }
    };

    BRCryptoTransfer transfer = cryptoTransferCreateAsETH (listener,
                                                           unit,
                                                           unitForFee,
                                                           estimatedFeeBasis,
                                                           amount,
                                                           direction,
                                                           source,
                                                           target,
                                                           transferState,
                                                           account,
                                                           basis,
                                                           NULL);

    cryptoFeeBasisGive(estimatedFeeBasis);
    cryptoAddressGive(source);
    cryptoAddressGive(target);

    return transfer;
}

extern BRCryptoTransfer
cryptoTransferCreateWithExchangeAsETH (BRCryptoTransferListener listener,
                                       BRCryptoUnit unit,
                                       BRCryptoUnit unitForFee,
                                       BREthereumAccount account,
                                       UInt256 ethAmount,
                                       OwnershipGiven BREthereumExchange ethExchange) {
    BREthereumAddress ethSource = ethExchangeGetSourceAddress (ethExchange);
    BREthereumAddress ethTarget = ethExchangeGetTargetAddress (ethExchange);

    BRCryptoTransferDirection direction = cryptoTransferFindDirection (account, ethSource, ethTarget);

    BRCryptoAmount  amount    = cryptoAmountCreate (unit, CRYPTO_FALSE, ethAmount);

    BREthereumFeeBasis ethFeeBasis = ethFeeBasisCreate (ethGasCreate(0),
                                                        ethGasPriceCreate(ethEtherCreateZero()));
    BRCryptoFeeBasis estimatedFeeBasis = cryptoFeeBasisCreateAsETH(unitForFee, ethFeeBasis);

    BRCryptoAddress  source = cryptoAddressCreateAsETH (ethSource);
    BRCryptoAddress  target = cryptoAddressCreateAsETH (ethTarget);

    BRCryptoTransferState transferState = cryptoTransferDeriveStateETH (ethExchangeGetStatus(ethExchange),
                                                                        estimatedFeeBasis);

    BREthereumTransferBasis basis = {
        TRANSFER_BASIS_EXCHANGE,
        { .exchange = ethExchange }
    };
    BRCryptoTransfer transfer = cryptoTransferCreateAsETH (listener,
                                                           unit,
                                                           unitForFee,
                                                           estimatedFeeBasis,
                                                           amount,
                                                           direction,
                                                           source,
                                                           target,
                                                           transferState,
                                                           account,
                                                           basis,
                                                           NULL);

    cryptoFeeBasisGive(estimatedFeeBasis);
    cryptoAddressGive(source);
    cryptoAddressGive(target);

    return transfer;
}

static void
cryptoTransferReleaseETH (BRCryptoTransfer transfer) {
    BRCryptoTransferETH transferETH = cryptoTransferCoerceETH (transfer);


    if (NULL != transferETH->originatingTransaction)
        transactionRelease(transferETH->originatingTransaction);

    switch (transferETH->basis.type) {
        case TRANSFER_BASIS_TRANSACTION:
            transactionRelease (transferETH->basis.u.transaction);
            break;

        case TRANSFER_BASIS_LOG:
            logRelease (transferETH->basis.u.log);
            break;

        case TRANSFER_BASIS_EXCHANGE:
            ethExchangeRelease (transferETH->basis.u.exchange);
            break;
    }
}

static BRCryptoTransferDirection
cryptoTransferFindDirection (BREthereumAccount account,
                             BREthereumAddress source,
                             BREthereumAddress target) {
    BREthereumBoolean accountIsSource = ethAccountHasAddress (account, source);
    BREthereumBoolean accountIsTarget = ethAccountHasAddress (account, target);

    if (accountIsSource == ETHEREUM_BOOLEAN_TRUE && accountIsTarget == ETHEREUM_BOOLEAN_TRUE) {
        return CRYPTO_TRANSFER_RECOVERED;
    } else if (accountIsSource == ETHEREUM_BOOLEAN_TRUE && accountIsTarget == ETHEREUM_BOOLEAN_FALSE) {
        return CRYPTO_TRANSFER_SENT;
    } else if (accountIsSource == ETHEREUM_BOOLEAN_FALSE && accountIsTarget == ETHEREUM_BOOLEAN_TRUE) {
        return CRYPTO_TRANSFER_RECEIVED;
    } else {
        assert(0);
    }
}

static BREthereumHash
cryptoTransferGetEthHash (BRCryptoTransfer transfer) {
    BRCryptoTransferETH transferETH = cryptoTransferCoerceETH(transfer);

    if (NULL != transferETH->originatingTransaction)
        return transactionGetHash (transferETH->originatingTransaction);

    switch (transferETH->basis.type) {
        case TRANSFER_BASIS_TRANSACTION:
            return (NULL == transferETH->basis.u.transaction ? EMPTY_HASH_INIT : transactionGetHash (transferETH->basis.u.transaction));
        case TRANSFER_BASIS_LOG:
            return (NULL == transferETH->basis.u.log         ? EMPTY_HASH_INIT : logGetIdentifier   (transferETH->basis.u.log));
        case TRANSFER_BASIS_EXCHANGE:
            return (NULL == transferETH->basis.u.exchange    ? EMPTY_HASH_INIT : ethExchangeGetIdentifier (transferETH->basis.u.exchange));
    }
}

static BRCryptoHash
cryptoTransferGetHashETH (BRCryptoTransfer transfer) {
    BREthereumHash ethHash = cryptoTransferGetEthHash (transfer);
    return (ETHEREUM_BOOLEAN_TRUE == ethHashEqual (ethHash, EMPTY_HASH_INIT)
            ? NULL
            : cryptoHashCreateAsETH (ethHash));
}

extern const BREthereumHash
cryptoTransferGetIdentifierETH (BRCryptoTransferETH transfer) {
    switch (transfer->basis.type) {
        case TRANSFER_BASIS_TRANSACTION:
            return (NULL == transfer->basis.u.transaction ? EMPTY_HASH_INIT : transactionGetHash(transfer->basis.u.transaction));
        case TRANSFER_BASIS_LOG:
            return (NULL == transfer->basis.u.log         ? EMPTY_HASH_INIT : logGetHash(transfer->basis.u.log));
        case TRANSFER_BASIS_EXCHANGE:
            return (NULL == transfer->basis.u.exchange    ? EMPTY_HASH_INIT : ethExchangeGetHash (transfer->basis.u.exchange));
    }
}

#if 0
static BREthereumHash
transferBasisGetHash (BREthereumTransferBasis *basis) {
    switch (basis->type) {
        case TRANSFER_BASIS_TRANSACTION: {
            if (NULL == basis->u.transaction) return EMPTY_HASH_INIT;

            return transactionGetHash (basis->u.transaction);
        }

        case TRANSFER_BASIS_LOG: {
            if (NULL == basis->u.log) return EMPTY_HASH_INIT;

            BREthereumHash hash = EMPTY_HASH_INIT;
            logExtractIdentifier(basis->u.log, &hash, NULL);
            return hash;
        }

        case TRANSFER_BASIS_EXCHANGE: {
            if (NULL == basis->u.exchange) return EMPTY_HASH_INIT;

            BREthereumHash hash = EMPTY_HASH_INIT;
            ethExchangeExtractIdentifier (basis->u.exchange, &hash, NULL);
            return hash;
        }
    }
}
#endif

extern const BREthereumHash
cryptoTransferGetOriginatingTransactionHashETH (BRCryptoTransferETH transfer) {
    // If we have an originatingTransaction - becasue we created the transfer - then return its
    // hash.  Otherwise use the transfer's basis to get the hash
    return  (NULL != transfer->originatingTransaction
             ? transactionGetHash (transfer->originatingTransaction)
             : (TRANSFER_BASIS_TRANSACTION == transfer->basis.type
                 ? (NULL == transfer->basis.u.transaction ? EMPTY_HASH_INIT : transactionGetHash (transfer->basis.u.transaction))
                 : (NULL == transfer->basis.u.log         ? EMPTY_HASH_INIT : logGetIdentifier   (transfer->basis.u.log))));
}

#if 0
extern uint64_t
transferGetNonce (BREthereumTransfer transfer) {
    return (NULL != transfer->originatingTransaction
            ? transactionGetNonce (transfer->originatingTransaction)
            : (TRANSFER_BASIS_TRANSACTION == transfer->basis.type && NULL != transfer->basis.u.transaction
               ? transactionGetNonce(transfer->basis.u.transaction)
               : TRANSACTION_NONCE_IS_NOT_ASSIGNED));
}
#endif

extern uint8_t *
cryptoTransferSerializeETH (BRCryptoTransfer transfer,
                            BRCryptoNetwork  network,
                            BRCryptoBoolean  requireSignature,
                            size_t *serializationCount) {
    BRCryptoTransferETH transferETH = cryptoTransferCoerceETH (transfer);

    if (NULL == transferETH->originatingTransaction ||
        (CRYPTO_TRUE == requireSignature &&
         ETHEREUM_BOOLEAN_FALSE == transactionIsSigned (transferETH->originatingTransaction))) {
        *serializationCount = 0;
        return NULL;
    }

    BRRlpData data = transactionGetRlpData (transferETH->originatingTransaction,
                                            cryptoNetworkAsETH(network),
                                            (CRYPTO_TRUE == requireSignature
                                             ? RLP_TYPE_TRANSACTION_SIGNED
                                             : RLP_TYPE_TRANSACTION_UNSIGNED));

    *serializationCount = data.bytesCount;
    return data.bytes;
}

extern uint8_t *
cryptoTransferGetBytesForFeeEstimateETH (BRCryptoTransfer transfer,
                                         BRCryptoNetwork  network,
                                         size_t *bytesCount) {
    BRCryptoTransferETH transferETH = cryptoTransferCoerceETH (transfer);
    BREthereumTransaction ethTransaction = transferETH->originatingTransaction;

    if (NULL == ethTransaction) { *bytesCount = 0; return NULL; }

    BRRlpData data = transactionGetRlpData (ethTransaction,
                                            cryptoNetworkAsETH(network),
                                            RLP_TYPE_TRANSACTION_UNSIGNED);
    BREthereumAddress ethSource = transactionGetSourceAddress (ethTransaction);

    *bytesCount = ADDRESS_BYTES + data.bytesCount;
    uint8_t *bytes = malloc (*bytesCount);
    memcpy (&bytes[0],             ethSource.bytes, ADDRESS_BYTES);
    memcpy (&bytes[ADDRESS_BYTES], data.bytes,      data.bytesCount);

    rlpDataRelease(data);

    return bytes;
}

static int
cryptoTransferEqualAsETH (BRCryptoTransfer tb1, BRCryptoTransfer tb2) {
    if (tb1 == tb2) return 1;

    BREthereumHash h1 = cryptoTransferGetEthHash (tb1);
    BREthereumHash h2 = cryptoTransferGetEthHash (tb2);

    return (ETHEREUM_BOOLEAN_FALSE != ethHashEqual (h1, EMPTY_HASH_INIT) &&
            ETHEREUM_BOOLEAN_TRUE  == ethHashEqual (h1, h2));
}

BRCryptoTransferHandlers cryptoTransferHandlersETH = {
    cryptoTransferReleaseETH,
    cryptoTransferGetHashETH,
    cryptoTransferSerializeETH,
    cryptoTransferGetBytesForFeeEstimateETH,
    cryptoTransferEqualAsETH
};
