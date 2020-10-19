//
//  BRCryptoTransfer.c
//  BRCore
//
//  Created by Ed Gamble on 3/19/19.
//  Copyright © 2019 breadwallet. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.

#include "support/BROSCompat.h"

#include "BRCryptoTransferP.h"

#include "BRCryptoBase.h"
#include "BRCryptoHashP.h"
#include "BRCryptoAddressP.h"
#include "BRCryptoAmountP.h"
#include "BRCryptoFeeBasisP.h"

#include "BRCryptoHandlersP.h"

/// MARK: - Transfer State Type

extern const char *
cryptoTransferStateTypeString (BRCryptoTransferStateType type) {
    static const char *strings[] = {
        "CRYPTO_TRANSFER_STATE_CREATED",
        "CRYPTO_TRANSFER_STATE_SIGNED",
        "CRYPTO_TRANSFER_STATE_SUBMITTED",
        "CRYPTO_TRANSFER_STATE_INCLUDED",
        "CRYPTO_TRANSFER_STATE_ERRORED",
        "CRYPTO_TRANSFER_STATE_DELETED",
    };
    assert (CRYPTO_TRANSFER_EVENT_CREATED <= type && type <= CRYPTO_TRANSFER_STATE_DELETED);
    return strings[type];
}

private_extern bool
cryptoTransferStateIsEqual (const BRCryptoTransferState *s1,
                            const BRCryptoTransferState *s2) {
    if (s1->type != s2->type) return false;

    switch (s1->type) {
        case CRYPTO_TRANSFER_STATE_INCLUDED:
            return (s1->u.included.blockNumber      == s2->u.included.blockNumber      &&
                    s1->u.included.transactionIndex == s2->u.included.transactionIndex &&
                    s1->u.included.timestamp        == s2->u.included.timestamp        &&
                    CRYPTO_TRUE == cryptoFeeBasisIsEqual (s1->u.included.feeBasis, s2->u.included.feeBasis) &&
                    s1->u.included.success          == s2->u.included.success);

        case CRYPTO_TRANSFER_STATE_ERRORED:
            return cryptoTransferSubmitErrorIsEqual (&s1->u.errored.error, &s2->u.errored.error);

        default:
            return true;
    }
}


/// MARK: Transfer

IMPLEMENT_CRYPTO_GIVE_TAKE (BRCryptoTransfer, cryptoTransfer)

extern BRCryptoTransfer
cryptoTransferAllocAndInit (size_t sizeInBytes,
                            BRCryptoBlockChainType type,
                            BRCryptoTransferListener listener,
                            BRCryptoUnit unit,
                            BRCryptoUnit unitForFee,
                            BRCryptoFeeBasis feeBasisEstimated,
                            BRCryptoAmount amount,
                            BRCryptoTransferDirection direction,
                            BRCryptoAddress sourceAddress,
                            BRCryptoAddress targetAddress,
                            BRCryptoTransferState state,
                            BRCryptoTransferCreateContext  createContext,
                            BRCryptoTransferCreateCallback createCallback) {
    assert (sizeInBytes >= sizeof (struct BRCryptoTransferRecord));
    assert (type == feeBasisEstimated->type);

    BRCryptoTransfer transfer = calloc (1, sizeInBytes);

    transfer->type  = type;
    transfer->handlers = cryptoHandlersLookup(type)->transfer;
    transfer->sizeInBytes = sizeInBytes;

    transfer->listener   = listener;
    transfer->state      = (BRCryptoTransferState) { CRYPTO_TRANSFER_STATE_CREATED };
    transfer->unit       = cryptoUnitTake(unit);
    transfer->unitForFee = cryptoUnitTake(unitForFee);
    transfer->feeBasisEstimated = cryptoFeeBasisTake (feeBasisEstimated);
    
    transfer->amount = cryptoAmountTake (amount);
    transfer->direction = direction;
    
    transfer->sourceAddress = cryptoAddressTake (sourceAddress);
    transfer->targetAddress = cryptoAddressTake (targetAddress);
    transfer->state         = cryptoTransferStateCopy (&state);

    array_new (transfer->attributes, 1);

    transfer->ref = CRYPTO_REF_ASSIGN (cryptoTransferRelease);

    pthread_mutex_init_brd (&transfer->lock, PTHREAD_MUTEX_NORMAL);

    if (NULL != createContext) createCallback (createContext, transfer);
    
    cryptoTransferGenerateEvent (transfer, (BRCryptoTransferEvent) {
        CRYPTO_TRANSFER_EVENT_CREATED
    });

    return transfer;
}

static void
cryptoTransferRelease (BRCryptoTransfer transfer) {
    pthread_mutex_lock (&transfer->lock);

    cryptoAddressGive (transfer->sourceAddress);
    cryptoAddressGive (transfer->targetAddress);
    cryptoUnitGive (transfer->unit);
    cryptoUnitGive (transfer->unitForFee);
    cryptoTransferStateRelease (&transfer->state);
    cryptoFeeBasisGive (transfer->feeBasisEstimated);
    cryptoAmountGive (transfer->amount);

    array_free_all(transfer->attributes, cryptoTransferAttributeGive);

    transfer->handlers->release (transfer);

    pthread_mutex_unlock  (&transfer->lock);
    pthread_mutex_destroy (&transfer->lock);

//    cryptoTransferGenerateEvent (transfer, (BRCryptoTransferEvent) {
//        CRYPTO_TRANSFER_EVENT_DELETED
//    });

    memset (transfer, 0, sizeof(*transfer));
    free (transfer);
}

private_extern BRCryptoBlockChainType
cryptoTransferGetType (BRCryptoTransfer transfer) {
    return transfer->type;
}

extern BRCryptoAddress
cryptoTransferGetSourceAddress (BRCryptoTransfer transfer) {
    return cryptoAddressTake (transfer->sourceAddress);
}

extern BRCryptoAddress
cryptoTransferGetTargetAddress (BRCryptoTransfer transfer) {
    return cryptoAddressTake (transfer->targetAddress);
}

static BRCryptoAmount
cryptoTransferGetAmountAsSign (BRCryptoTransfer transfer, BRCryptoBoolean isNegative) {
    return NULL == transfer->amount ? NULL : cryptoAmountCreate (cryptoAmountGetUnit(transfer->amount),
                                                                 isNegative,
                                                                 cryptoAmountGetValue(transfer->amount));
}

extern BRCryptoAmount
cryptoTransferGetAmount (BRCryptoTransfer transfer) {
    return cryptoAmountTake (transfer->amount);
}

extern BRCryptoAmount
cryptoTransferGetAmountDirected (BRCryptoTransfer transfer) {
    BRCryptoAmount   amount;

    switch (cryptoTransferGetDirection(transfer)) {
        case CRYPTO_TRANSFER_RECOVERED: {
            amount = cryptoAmountCreate (transfer->unit,
                                         CRYPTO_FALSE,
                                         UINT256_ZERO);
            break;
        }

        case CRYPTO_TRANSFER_SENT: {
            amount = cryptoTransferGetAmountAsSign (transfer,
                                                    CRYPTO_TRUE);
            break;
        }

        case CRYPTO_TRANSFER_RECEIVED: {
            amount = cryptoTransferGetAmountAsSign (transfer,
                                                    CRYPTO_FALSE);
            break;
        }
        default: assert(0);
    }

    return amount;
}

extern BRCryptoAmount
cryptoTransferGetAmountDirectedNet (BRCryptoTransfer transfer) {
    BRCryptoAmount amount = cryptoTransferGetAmountDirected (transfer);
    BRCryptoAmount amountNet;
    
    switch (cryptoTransferGetDirection(transfer)) {
        case CRYPTO_TRANSFER_RECOVERED:
        case CRYPTO_TRANSFER_SENT: {
            BRCryptoAmount fee = cryptoTransferGetFee (transfer);
            amountNet = (NULL == fee) ? cryptoAmountTake (amount) : cryptoAmountSub (amount, fee);
            cryptoAmountGive (fee);
            break;
        }

        case CRYPTO_TRANSFER_RECEIVED: {
            amountNet = cryptoAmountTake (amount);
            break;
        }
        default: assert(0);
    }
    
    cryptoAmountGive (amount);
    
    return amountNet;
}

extern BRCryptoUnit
cryptoTransferGetUnitForAmount (BRCryptoTransfer transfer) {
    return cryptoUnitTake (transfer->unit);
}

extern BRCryptoUnit
cryptoTransferGetUnitForFee (BRCryptoTransfer transfer) {
    return cryptoUnitTake (transfer->unitForFee);
}

extern size_t
cryptoTransferGetAttributeCount (BRCryptoTransfer transfer) {
    pthread_mutex_lock (&transfer->lock);
    size_t count = array_count(transfer->attributes);
    pthread_mutex_unlock (&transfer->lock);
    return count;
}

extern BRCryptoTransferAttribute
cryptoTransferGetAttributeAt (BRCryptoTransfer transfer,
                              size_t index) {
    pthread_mutex_lock (&transfer->lock);
    BRCryptoTransferAttribute attribute = cryptoTransferAttributeTake (transfer->attributes[index]);
    pthread_mutex_unlock (&transfer->lock);
    return attribute;
}

private_extern void
cryptoTransferSetAttributes (BRCryptoTransfer transfer,
                             size_t attributesCount,
                             OwnershipKept BRCryptoTransferAttribute *attributes) {
    pthread_mutex_lock (&transfer->lock);

    // Give existing attributes and empty `transfer->attributes`
    for (size_t index = 0; index < array_count(transfer->attributes); index++)
        cryptoTransferAttributeGive (transfer->attributes[index]);
    array_clear(transfer->attributes);

    if (NULL != attributes)
        // Take new attributes.
        for (size_t index = 0; index < attributesCount; index++)
            array_add (transfer->attributes, cryptoTransferAttributeTake (attributes[index]));
    pthread_mutex_unlock (&transfer->lock);
}


extern BRCryptoTransferStateType
cryptoTransferGetStateType (BRCryptoTransfer transfer) {
    return transfer->state.type;
}

extern BRCryptoTransferState
cryptoTransferGetState (BRCryptoTransfer transfer) {
    pthread_mutex_lock (&transfer->lock);
    BRCryptoTransferState state = cryptoTransferStateCopy (&transfer->state);
    pthread_mutex_unlock (&transfer->lock);

    return state;
}

private_extern void
cryptoTransferSetState (BRCryptoTransfer transfer,
                        BRCryptoTransferState state) {
    BRCryptoTransferState newState = cryptoTransferStateCopy (&state);

    pthread_mutex_lock (&transfer->lock);
    BRCryptoTransferState oldState = transfer->state;
    transfer->state = newState;
    pthread_mutex_unlock (&transfer->lock);

    if (!cryptoTransferStateIsEqual (&oldState, &newState)) {
        // A Hack: Instead Wallet shouild listen for CRYPTO_TRANSFER_EVENT_CHANGED
        if (NULL != transfer->listener.transferChangedCallback)
            transfer->listener.transferChangedCallback (transfer->listener.wallet, transfer, newState);

        cryptoTransferGenerateEvent (transfer, (BRCryptoTransferEvent) {
            CRYPTO_TRANSFER_EVENT_CHANGED,
            { .state = {
                cryptoTransferStateCopy (&oldState),
                cryptoTransferStateCopy (&newState) }}
        });
    }
    
    cryptoTransferStateRelease (&oldState);
}

extern BRCryptoTransferDirection
cryptoTransferGetDirection (BRCryptoTransfer transfer) {
    return transfer->direction;
}


extern BRCryptoHash
cryptoTransferGetHash (BRCryptoTransfer transfer) {
    return transfer->handlers->getHash (transfer);
}

extern BRCryptoFeeBasis
cryptoTransferGetEstimatedFeeBasis (BRCryptoTransfer transfer) {
    return cryptoFeeBasisTake (transfer->feeBasisEstimated);
}

private_extern BRCryptoAmount
cryptoTransferGetEstimatedFee (BRCryptoTransfer transfer) {
    // TODO: NULL on unit vs unitForFee mismatch?
   return (NULL != transfer->feeBasisEstimated
            ? cryptoFeeBasisGetFee (transfer->feeBasisEstimated)
            : NULL);
}

extern BRCryptoFeeBasis
cryptoTransferGetConfirmedFeeBasis (BRCryptoTransfer transfer) {
    pthread_mutex_lock (&transfer->lock);
    BRCryptoFeeBasis feeBasisConfirmed = (CRYPTO_TRANSFER_STATE_INCLUDED == transfer->state.type
                                          ? cryptoFeeBasisTake (transfer->state.u.included.feeBasis)
                                          : NULL);
    pthread_mutex_unlock (&transfer->lock);

    return feeBasisConfirmed;
}

private_extern BRCryptoAmount
cryptoTransferGetConfirmedFee (BRCryptoTransfer transfer) {
    // TODO: NULL on unit vs unitForFee mismatch?
    return ((CRYPTO_TRANSFER_STATE_INCLUDED == transfer->state.type &&
             NULL != transfer->state.u.included.feeBasis)
            ? cryptoFeeBasisGetFee (transfer->state.u.included.feeBasis)
            : NULL);
}

private_extern BRCryptoFeeBasis
cryptoTransferGetFeeBasis (BRCryptoTransfer transfer) {
    return cryptoFeeBasisTake (CRYPTO_TRANSFER_STATE_INCLUDED == transfer->state.type
                               ? transfer->state.u.included.feeBasis
                               : transfer->feeBasisEstimated);
}

extern BRCryptoAmount
cryptoTransferGetFee (BRCryptoTransfer transfer) {
    if (CRYPTO_FALSE == cryptoUnitIsCompatible (transfer->unit, transfer->unitForFee))
        return NULL;

    BRCryptoFeeBasis feeBasis = (CRYPTO_TRANSFER_STATE_INCLUDED == transfer->state.type
                                 ? transfer->state.u.included.feeBasis
                                 : transfer->feeBasisEstimated);

    return (NULL == feeBasis ? NULL : cryptoFeeBasisGetFee (feeBasis));
}

private_extern BRCryptoTransferListener
cryptoTransferGetListener (BRCryptoTransfer transfer) {
    return transfer->listener;
}

private_extern void
cryptoTransferSetListener (BRCryptoTransfer transfer,
                           BRCryptoTransferListener listener) {
    transfer->listener = listener;
}

extern uint8_t *
cryptoTransferSerializeForSubmission (BRCryptoTransfer transfer,
                                      BRCryptoNetwork  network,
                                      size_t *serializationCount) {
    assert (NULL != serializationCount);
    return transfer->handlers->serialize (transfer, network, CRYPTO_TRUE, serializationCount);
}

extern uint8_t *
cryptoTransferSerializeForFeeEstimation (BRCryptoTransfer transfer,
                                         BRCryptoNetwork  network,
                                         size_t *bytesCount) {
    assert (NULL != bytesCount);
    return (NULL != transfer->handlers->getBytesForFeeEstimate
            ? transfer->handlers->getBytesForFeeEstimate (transfer, network, bytesCount)
            : transfer->handlers->serialize (transfer, network, CRYPTO_FALSE, bytesCount));
}

extern BRCryptoBoolean
cryptoTransferEqual (BRCryptoTransfer t1, BRCryptoTransfer t2) {
    return AS_CRYPTO_BOOLEAN (t1 == t2 ||
            (t1->type == t2->type &&
             t1->handlers->isEqual (t1, t2)));
}

extern BRCryptoComparison
cryptoTransferCompare (BRCryptoTransfer transfer1, BRCryptoTransfer transfer2) {
    // early bail when comparing the same transfer
    if (CRYPTO_TRUE == cryptoTransferEqual (transfer1, transfer2)) {
        return CRYPTO_COMPARE_EQ;
    }

    // The algorithm below is captured in the cryptoTransferCompare declaration
    // comments; any changes to this routine must be reflected in that comment
    // and vice versa).
    //
    // The algorithm includes timestamp as a differentiator despite the fact that
    // timestamp is likely derived from the block. Thus, an occurrence where timestamp
    // is different while block value is the same is unlikely. Regardless, this check
    // is included to handle cases where that assumption does not hold.
    //
    // Another reason to include timestamp is if this function were used to order
    // transfers across different wallets. While not anticipated to be a common use
    // case, there is not enough information available in the transfer object to
    // preclude it from happening. Checking on the `type` field is insufficient
    // given that GEN will handle multiple cases. While block number and transaction
    // index are meaningless comparables between wallets, ordering by timestamp
    // does provide some value.

    BRCryptoComparison compareValue;
    BRCryptoTransferState state1 = cryptoTransferGetState (transfer1);
    BRCryptoTransferState state2 = cryptoTransferGetState (transfer2);

    // neither transfer is included
    if (state1.type != CRYPTO_TRANSFER_STATE_INCLUDED &&
        state2.type != CRYPTO_TRANSFER_STATE_INCLUDED) {
        // we don't have anything to sort on other than identity
        compareValue = (uintptr_t) transfer1 > (uintptr_t) transfer2 ?
            CRYPTO_COMPARE_GT : CRYPTO_COMPARE_LT;

    // transfer1 is NOT included (and transfer2 is)
    } else if (state1.type != CRYPTO_TRANSFER_STATE_INCLUDED) {
        // return "greater than" for transfer1
        compareValue = CRYPTO_COMPARE_GT;

    // transfer2 is NOT included (and transfer1 is)
    } else if (state2.type != CRYPTO_TRANSFER_STATE_INCLUDED) {
        // return "lesser than" for transfer1
        compareValue = CRYPTO_COMPARE_LT;

    // both are included, check if the timestamp differs
    } else if (state1.u.included.timestamp != state2.u.included.timestamp) {
        // return based on the greater timestamp
        compareValue = state1.u.included.timestamp > state2.u.included.timestamp ?
            CRYPTO_COMPARE_GT : CRYPTO_COMPARE_LT;

    // both are included and have the same timestamp, check if the block differs
    } else if (state1.u.included.blockNumber != state2.u.included.blockNumber) {
        // return based on the greater block number
        compareValue = state1.u.included.blockNumber > state2.u.included.blockNumber ?
            CRYPTO_COMPARE_GT : CRYPTO_COMPARE_LT;

    // both are included and have the same timestamp and block, check if the index differs
    } else if (state1.u.included.transactionIndex != state2.u.included.transactionIndex) {
        // return based on the greater index
        compareValue = state1.u.included.transactionIndex > state2.u.included.transactionIndex ?
            CRYPTO_COMPARE_GT : CRYPTO_COMPARE_LT;

    // both are included and have the same timestamp, block and index
    } else {
        // we are out of differentiators, return "equal"
        compareValue = CRYPTO_COMPARE_EQ;
    }

    // clean up on the way out
    cryptoTransferStateRelease (&state1);
    cryptoTransferStateRelease (&state2);
    return compareValue;
}

extern void
cryptoTransferExtractBlobAsBTC (BRCryptoTransfer transfer,
                                uint8_t **bytes,
                                size_t   *bytesCount,
                                uint32_t *blockHeight,
                                uint32_t *timestamp) {
    #ifdef REFACTOR
    assert (NULL != bytes && NULL != bytesCount);

    BRTransaction *tx = cryptoTransferAsBTC (transfer);

    *bytesCount = BRTransactionSerialize (tx, NULL, 0);
    *bytes = malloc (*bytesCount);
    BRTransactionSerialize (tx, *bytes, *bytesCount);

    if (NULL != blockHeight) *blockHeight = tx->blockHeight;
    if (NULL != timestamp)   *timestamp   = tx->timestamp;
    #endif
}

extern BRCryptoTransferState
cryptoTransferStateInit (BRCryptoTransferStateType type) {
    switch (type) {
        case CRYPTO_TRANSFER_STATE_CREATED:
        case CRYPTO_TRANSFER_STATE_DELETED:
        case CRYPTO_TRANSFER_STATE_SIGNED:
        case CRYPTO_TRANSFER_STATE_SUBMITTED: {
            return (BRCryptoTransferState) {
                type
            };
        }
        case CRYPTO_TRANSFER_STATE_INCLUDED:
            assert (0); // if you are hitting this, use cryptoTransferStateIncludedInit!
            return (BRCryptoTransferState) {
                CRYPTO_TRANSFER_STATE_INCLUDED,
                { .included = { 0, 0, 0, NULL }}
            };
        case CRYPTO_TRANSFER_STATE_ERRORED: {
            assert (0); // if you are hitting this, use cryptoTransferStateErroredInit!
            return (BRCryptoTransferState) {
                CRYPTO_TRANSFER_STATE_ERRORED,
                { .errored = { cryptoTransferSubmitErrorUnknown() }}
            };
        }
    }
}

extern BRCryptoTransferState
cryptoTransferStateIncludedInit (uint64_t blockNumber,
                                 uint64_t transactionIndex,
                                 uint64_t timestamp,
                                 OwnershipKept BRCryptoFeeBasis feeBasis,
                                 BRCryptoBoolean success,
                                 const char *error) {
    BRCryptoTransferState result = (BRCryptoTransferState) {
        CRYPTO_TRANSFER_STATE_INCLUDED,
        { .included = {
            blockNumber,
            transactionIndex,
            timestamp,
            cryptoFeeBasisTake(feeBasis),
            success
        }}
    };

    memset (result.u.included.error, 0, CRYPTO_TRANSFER_INCLUDED_ERROR_SIZE + 1);
    if (CRYPTO_FALSE == success) {
        strlcpy (result.u.included.error,
                 (NULL == error ? "unknown error" : error),
                 CRYPTO_TRANSFER_INCLUDED_ERROR_SIZE + 1);
    }

    return result;
}

extern BRCryptoTransferState
cryptoTransferStateErroredInit (BRCryptoTransferSubmitError error) {
    return (BRCryptoTransferState) {
        CRYPTO_TRANSFER_STATE_ERRORED,
        { .errored = { error }}
    };
}

extern BRCryptoTransferState
cryptoTransferStateCopy (BRCryptoTransferState *state) {
    BRCryptoTransferState newState = *state;
    switch (state->type) {
        case CRYPTO_TRANSFER_STATE_INCLUDED: {
            if (NULL != newState.u.included.feeBasis) {
                cryptoFeeBasisTake (newState.u.included.feeBasis);
            }
            break;
        }
        case CRYPTO_TRANSFER_STATE_ERRORED:
        case CRYPTO_TRANSFER_STATE_CREATED:
        case CRYPTO_TRANSFER_STATE_DELETED:
        case CRYPTO_TRANSFER_STATE_SIGNED:
        case CRYPTO_TRANSFER_STATE_SUBMITTED:
        default: {
            break;
        }
    }
    return newState;
}

extern void
cryptoTransferStateRelease (BRCryptoTransferState *state) {
    switch (state->type) {
        case CRYPTO_TRANSFER_STATE_INCLUDED: {
            if (NULL != state->u.included.feeBasis) {
                cryptoFeeBasisGive (state->u.included.feeBasis);
            }
            break;
        }
        case CRYPTO_TRANSFER_STATE_ERRORED:
        case CRYPTO_TRANSFER_STATE_CREATED:
        case CRYPTO_TRANSFER_STATE_DELETED:
        case CRYPTO_TRANSFER_STATE_SIGNED:
        case CRYPTO_TRANSFER_STATE_SUBMITTED:
        default: {
            break;
        }
    }

    memset (state, 0, sizeof(*state));
}

static BRRlpItem
cryptoTransferStateRLPEncode (const BRCryptoTransferState *state,
                              BRCryptoNetwork network,
                              BRRlpCoder coder) {
    switch (state->type) {
        case CRYPTO_TRANSFER_STATE_INCLUDED:
            return rlpEncodeList (coder, 7,
                                  rlpEncodeUInt64 (coder, state->type, 0),
                                  rlpEncodeUInt64 (coder, state->u.included.blockNumber,      0),
                                  rlpEncodeUInt64 (coder, state->u.included.transactionIndex, 0),
                                  rlpEncodeUInt64 (coder, state->u.included.timestamp,        0),
                                  cryptoNetworkRLPEncodeFeeBasis (network, state->u.included.feeBasis, coder),
                                  rlpEncodeUInt64 (coder, state->u.included.success,          0),
                                  rlpEncodeString (coder, state->u.included.error));

        case CRYPTO_TRANSFER_STATE_ERRORED:
            return rlpEncodeList2 (coder,
                                   rlpEncodeUInt64 (coder, state->type, 0),
                                   rlpEncodeList2  (coder,
                                                    rlpEncodeUInt64 (coder, state->u.errored.error.type, 0),
                                                    rlpEncodeUInt64 (coder, (uint64_t) state->u.errored.error.u.posix.errnum, 0)));

        default:
            return rlpEncodeList1 (coder,
                                   rlpEncodeUInt64 (coder, state->type, 0));
    }
}

static BRCryptoTransferState
cryptoTransferStateRLPDecode (BRRlpItem item,
                              BRCryptoNetwork network,
                              BRRlpCoder coder){
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (itemsCount >= 1);

    BRCryptoTransferStateType type = (BRCryptoTransferStateType) rlpDecodeUInt64 (coder, items[0], 0);
    switch (type) {
        case CRYPTO_TRANSFER_STATE_INCLUDED: {
            assert (7 == itemsCount);

            uint64_t blockNumber      = rlpDecodeUInt64 (coder, items[1], 0);
            uint64_t transactionIndex = rlpDecodeUInt64 (coder, items[2], 0);
            uint64_t timestamp        = rlpDecodeUInt64 (coder, items[3], 0);
            BRCryptoFeeBasis feeBasis = cryptoNetworkRLPDecodeFeeBasis (network, items[4], coder);
            BRCryptoBoolean  success  = (BRCryptoBoolean) rlpDecodeUInt64 (coder, items[5], 0);

            char *error = rlpDecodeString (coder, items[6]);

            BRCryptoTransferState state = cryptoTransferStateIncludedInit (blockNumber,
                                                                           transactionIndex,
                                                                           timestamp,
                                                                           feeBasis,
                                                                           success,
                                                                           error);

            free (error);
            cryptoFeeBasisGive(feeBasis);

            return state;
        }

        case CRYPTO_TRANSFER_STATE_ERRORED: {
            assert (2 == itemsCount);
            size_t errorItemsCount;
            const BRRlpItem *errorItems = rlpDecodeList (coder, items[1], &errorItemsCount);
            assert (2 == errorItemsCount);

            BRCryptoTransferSubmitErrorType errType = (BRCryptoTransferSubmitErrorType) rlpDecodeUInt64 (coder, errorItems[0], 0);
            int errNum = (int) rlpDecodeUInt64 (coder, errorItems[1], 0);

            return cryptoTransferStateErroredInit ((BRCryptoTransferSubmitError) {
                errType,
                { .posix = { errNum }}
            });
        }

        default: {
            assert (1 == itemsCount);
            return cryptoTransferStateInit (type);
        }
    }
}

extern const char *
cryptoTransferEventTypeString (BRCryptoTransferEventType t) {
    switch (t) {
        case CRYPTO_TRANSFER_EVENT_CREATED:
        return "CRYPTO_TRANSFER_EVENT_CREATED";

        case CRYPTO_TRANSFER_EVENT_CHANGED:
        return "CRYPTO_TRANSFER_EVENT_CHANGED";

        case CRYPTO_TRANSFER_EVENT_DELETED:
        return "CRYPTO_TRANSFER_EVENT_DELETED";
    }
    return "<CRYPTO_TRANSFER_EVENT_TYPE_UNKNOWN>";
}

/// MARK: Transaction Submission Error

// TODO(fix): This should be moved to a more appropriate file (BRTransfer.c/h?)

extern BRCryptoTransferSubmitError
cryptoTransferSubmitErrorUnknown(void) {
    return (BRCryptoTransferSubmitError) {
        CRYPTO_TRANSFER_SUBMIT_ERROR_UNKNOWN
    };
}

extern BRCryptoTransferSubmitError
cryptoTransferSubmitErrorPosix(int errnum) {
    return (BRCryptoTransferSubmitError) {
        CRYPTO_TRANSFER_SUBMIT_ERROR_POSIX,
        { .posix = { errnum } }
    };
}

extern bool
cryptoTransferSubmitErrorIsEqual (const BRCryptoTransferSubmitError *e1,
                                  const BRCryptoTransferSubmitError *e2) {
    return (e1->type == e2->type &&
            (e1->type != CRYPTO_TRANSFER_SUBMIT_ERROR_POSIX ||
             e1->u.posix.errnum == e2->u.posix.errnum));
}

extern char *
cryptoTransferSubmitErrorGetMessage (BRCryptoTransferSubmitError *e) {
    char *message = NULL;

    switch (e->type) {
        case CRYPTO_TRANSFER_SUBMIT_ERROR_POSIX: {
            if (NULL != (message = strerror (e->u.posix.errnum))) {
                message = strdup (message);
            }
            break;
        }
        default: {
            break;
        }
    }

    return message;
}


/// MARK: - Transfer Attribute

DECLARE_CRYPTO_GIVE_TAKE (BRCryptoTransferAttribute, cryptoTransferAttribute);

struct BRCryptoTransferAttributeRecord {
    char *key;
    char *value;
    BRCryptoBoolean isRequired;
    BRCryptoRef ref;
};

IMPLEMENT_CRYPTO_GIVE_TAKE (BRCryptoTransferAttribute, cryptoTransferAttribute)

private_extern BRCryptoTransferAttribute
cryptoTransferAttributeCreate (const char *key,
                               const char *val,
                               BRCryptoBoolean isRequired) {
    BRCryptoTransferAttribute attribute = calloc (1, sizeof (struct BRCryptoTransferAttributeRecord));

    attribute->key   = strdup (key);
    attribute->value = (NULL == val ? NULL : strdup (val));
    attribute->isRequired = isRequired;

    attribute->ref = CRYPTO_REF_ASSIGN (cryptoTransferAttributeRelease);

    return attribute;
}

extern BRCryptoTransferAttribute
cryptoTransferAttributeCopy (BRCryptoTransferAttribute attribute) {
    return cryptoTransferAttributeCreate (attribute->key,
                                          attribute->value,
                                          attribute->isRequired);
}

static void
cryptoTransferAttributeRelease (BRCryptoTransferAttribute attribute) {
    free (attribute->key);
    if (NULL != attribute->value) free (attribute->value);
    memset (attribute, 0, sizeof (struct BRCryptoTransferAttributeRecord));
    free (attribute);
}

extern const char *
cryptoTransferAttributeGetKey (BRCryptoTransferAttribute attribute) {
    return attribute->key;
}

extern const char * // nullable
cryptoTransferAttributeGetValue (BRCryptoTransferAttribute attribute) {
    return attribute->value;
}
extern void
cryptoTransferAttributeSetValue (BRCryptoTransferAttribute attribute, const char *value) {
    if (NULL != attribute->value) free (attribute->value);
    attribute->value = (NULL == value ? NULL : strdup (value));
}

extern BRCryptoBoolean
cryptoTransferAttributeIsRequired (BRCryptoTransferAttribute attribute) {
    return attribute->isRequired;
}

private_extern void
cryptoTransferAttributeArrayRelease (BRArrayOf(BRCryptoTransferAttribute) attributes) {
    if (NULL == attributes) return;
    array_free_all (attributes, cryptoTransferAttributeGive);
}

static BRRlpItem
cryptoTransferAttributeRLPEncode (BRCryptoTransferAttribute attribute,
                                  BRRlpCoder coder) {
    return rlpEncodeList (coder, 3,
                          rlpEncodeString (coder, attribute->key),
                          rlpEncodeString (coder, attribute->value),
                          rlpEncodeUInt64 (coder, attribute->isRequired, 0));
}

static BRCryptoTransferAttribute
cryptoTransferAttributeRLPDecode (BRRlpItem item,
                                  BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (3 == itemsCount);

    char *key = rlpDecodeString (coder, items[0]);
    char *val = rlpDecodeString (coder, items[1]);
    BRCryptoBoolean isRequired = (BRCryptoBoolean) rlpDecodeUInt64 (coder, items[2], 0);

    BRCryptoTransferAttribute attribute = cryptoTransferAttributeCreate (key, val, isRequired);

    free (val);
    free (key);

    return attribute;
}

static BRRlpItem
cryptoTransferAttributesRLPEncode (BRArrayOf(BRCryptoTransferAttribute) attributes,
                                   BRRlpCoder coder) {
    size_t itemsCount = array_count(attributes);
    BRRlpItem items[itemsCount];

    for (size_t index = 0; index < itemsCount; index++)
        items[index] = cryptoTransferAttributeRLPEncode (attributes[index], coder);

    return rlpEncodeListItems (coder, items, itemsCount);
}

static BRArrayOf(BRCryptoTransferAttribute)
cryptoTransferAttributesRLPDecode (BRRlpItem item,
                                   BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);

    BRArrayOf(BRCryptoTransferAttribute) attributes;
    array_new(attributes, itemsCount);

    for (size_t index = 0; index < itemsCount; index++)
    array_add (attributes, cryptoTransferAttributeRLPDecode (items[index], coder));

    return attributes;
}


// MARK: - RLP Encode

private_extern BRRlpItem
cryptoTransferRLPEncodeBase (BRCryptoTransfer transfer,
                             BRCryptoNetwork  network,
                             BRRlpCoder coder) {
    return rlpEncodeList (coder, 11,
                          rlpEncodeUInt64 (coder, (uint64_t) transfer->sizeInBytes, 0),
                          cryptoBlockChainTypeRLPEncode     (transfer->type,                       coder),
                          cryptoNetworkRLPEncodeAddress     (network, transfer->sourceAddress,     coder),
                          cryptoNetworkRLPEncodeAddress     (network, transfer->targetAddress,     coder),
                          cryptoTransferStateRLPEncode      (&transfer->state, network,            coder),
                          cryptoNetworkRLPEncodeUnit        (network, transfer->unit,              coder),
                          cryptoNetworkRLPEncodeUnit        (network, transfer->unitForFee,        coder),
                          cryptoNetworkRLPEncodeFeeBasis    (network, transfer->feeBasisEstimated, coder),
                          rlpEncodeUInt64 (coder, (uint64_t) transfer->direction, 0),
                          cryptoNetworkRLPEncodeAmount      (network, transfer->amount,            coder),
                          cryptoTransferAttributesRLPEncode (transfer->attributes,                 coder));
}

private_extern BRCryptoTransfer
cryptoTransferRLPDecodeBase (BRRlpItem item,
                             BRCryptoNetwork  network,
                             BRCryptoTransferCreateContext  createContext,
                             BRCryptoTransferCreateCallback createCallback,
                             BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (11 == itemsCount);

    size_t sizeInBytes = (size_t) rlpDecodeUInt64 (coder, items[0], 0);

    BRCryptoBlockChainType type = cryptoBlockChainTypeRLPDecode (items[1], coder);

    BRCryptoAddress sourceAddress = cryptoNetworkRLPDecodeAddress (network, items[2], coder);
    BRCryptoAddress targetAddress = cryptoNetworkRLPDecodeAddress (network, items[3], coder);

    BRCryptoTransferState state = cryptoTransferStateRLPDecode (items[4], network, coder);

    BRCryptoUnit unit       = cryptoNetworkRLPDecodeUnit (network, items[5], coder);
    BRCryptoUnit unitForFee = cryptoNetworkRLPDecodeUnit (network, items[6], coder);

    BRCryptoFeeBasis feeBasisEstimated = cryptoNetworkRLPDecodeFeeBasis (network, items[7], coder);

    BRCryptoTransferDirection direction = (BRCryptoTransferDirection) rlpDecodeUInt64 (coder, items[8], 0);

    BRCryptoAmount amount = cryptoNetworkRLPDecodeAmount (network, items[9], coder);

    BRArrayOf(BRCryptoTransferAttribute) attributes = cryptoTransferAttributesRLPDecode (items[10], coder);

    BRCryptoTransfer transfer = cryptoTransferAllocAndInit (sizeInBytes,
                                                            type,
                                                            CRYPTO_TRANSFER_LISTENER_EMPTY,
                                                            unit,
                                                            unitForFee,
                                                            feeBasisEstimated,
                                                            amount,
                                                            direction,
                                                            sourceAddress,
                                                            targetAddress,
                                                            state,
                                                            createContext,
                                                            createCallback);

    array_free_all (attributes, cryptoTransferAttributeGive);
    cryptoAmountGive (amount);
    cryptoFeeBasisGive (feeBasisEstimated);
    cryptoUnitGive     (unitForFee);
    cryptoUnitGive     (unit);
    cryptoTransferStateRelease (&state);
    cryptoAddressGive  (targetAddress);
    cryptoAddressGive  (sourceAddress);

    return transfer;
}

private_extern BRRlpItem
cryptoTransferRLPEncode (BRCryptoTransfer transfer,
                         BRCryptoNetwork  network,
                         BRRlpCoder coder) {
    return transfer->handlers->encodeRLP (transfer, network, coder);
}

private_extern BRCryptoTransfer
cryptoTransferRLPDecode (BRRlpItem item,
                         BRCryptoNetwork  network,
                         BRRlpCoder coder) {
    const BRCryptoHandlers *handlers = cryptoHandlersLookup (network->type);
    return handlers->transfer->decodeRLP (item, network, coder);
}

static size_t
cryptoTransferGetHashValue (BRCryptoTransfer transfer) {
    BRCryptoHash hash = cryptoTransferGetHash (transfer);
    size_t value = (size_t) cryptoHashGetHashValue(hash);
    cryptoHashGive (hash);
    return value;
}

private_extern BRSetOf (BRCryptoTransfer)
cryptoTransferSetCreate (size_t count) {
    return BRSetNew ((size_t (*) (const void *)) cryptoTransferGetHashValue,
                     (int (*) (const void *, const void *))cryptoTransferEqual,
                     count);
}

private_extern void
cryptoTransferSetRelease (BRSetOf(BRCryptoTransfer) transfers) {
    BRSetFreeAll (transfers, (void (*) (void *)) cryptoTransferGive);
}

