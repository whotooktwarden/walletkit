//
//  BRCryptoFeeBasisXTZ.c
//  Core
//
//  Created by Ehsan Rezaie on 2020-09-04.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include "BRCryptoXTZ.h"
#include "crypto/BRCryptoFeeBasisP.h"
#include "tezos/BRTezos.h"

private_extern BRCryptoFeeBasisXTZ
cryptoFeeBasisCoerceXTZ (BRCryptoFeeBasis feeBasis) {
    assert (CRYPTO_NETWORK_TYPE_XTZ == feeBasis->type);
    return (BRCryptoFeeBasisXTZ) feeBasis;
}

typedef struct {
    BRTezosFeeBasis xtzFeeBasis;
} BRCryptoFeeBasisCreateContextXTZ;

static void
cryptoFeeBasisCreateCallbackXTZ (BRCryptoFeeBasisCreateContext context,
                                 BRCryptoFeeBasis feeBasis) {
    BRCryptoFeeBasisCreateContextXTZ *contextXTZ = (BRCryptoFeeBasisCreateContextXTZ*) context;
    BRCryptoFeeBasisXTZ feeBasisXTZ = cryptoFeeBasisCoerceXTZ (feeBasis);
    
    feeBasisXTZ->xtzFeeBasis = contextXTZ->xtzFeeBasis;
}

private_extern BRCryptoFeeBasis
cryptoFeeBasisCreateAsXTZ (BRCryptoUnit unit,
                           BRTezosFeeBasis xtzFeeBasis) {
    BRCryptoFeeBasisCreateContextXTZ contextXTZ = {
        xtzFeeBasis
    };
    
    return cryptoFeeBasisAllocAndInit (sizeof (struct BRCryptoFeeBasisXTZRecord),
                                       CRYPTO_NETWORK_TYPE_XTZ,
                                       unit,
                                       &contextXTZ,
                                       cryptoFeeBasisCreateCallbackXTZ);
}

static void
cryptoFeeBasisReleaseXTZ (BRCryptoFeeBasis feeBasis) {
}

static double
cryptoFeeBasisGetCostFactorXTZ (BRCryptoFeeBasis feeBasis) {
    BRTezosFeeBasis xtzFeeBasis = cryptoFeeBasisCoerceXTZ (feeBasis)->xtzFeeBasis;
    switch (xtzFeeBasis.type) {
        case FEE_BASIS_ESTIMATE:
            return (double) xtzFeeBasis.u.estimate.sizeInBytes;
        case FEE_BASIS_ACTUAL:
            return 1.0;
    }
}

static BRCryptoAmount
cryptoFeeBasisGetPricePerCostFactorXTZ (BRCryptoFeeBasis feeBasis) {
    BRTezosFeeBasis xtzFeeBasis = cryptoFeeBasisCoerceXTZ (feeBasis)->xtzFeeBasis;
    switch (xtzFeeBasis.type) {
        case FEE_BASIS_ESTIMATE:
            return cryptoAmountCreateAsXTZ (feeBasis->unit, CRYPTO_FALSE, xtzFeeBasis.u.estimate.mutezPerByte);
        case FEE_BASIS_ACTUAL:
            return cryptoAmountCreateAsXTZ (feeBasis->unit, CRYPTO_FALSE, xtzFeeBasis.u.actual.fee);
    }
}

static BRCryptoAmount
cryptoFeeBasisGetFeeXTZ (BRCryptoFeeBasis feeBasis) {
    BRTezosFeeBasis xtzFeeBasis = cryptoFeeBasisCoerceXTZ (feeBasis)->xtzFeeBasis;
    BRTezosUnitMutez fee = tezosFeeBasisGetFee (&xtzFeeBasis);
    return cryptoAmountCreateAsXTZ (feeBasis->unit, CRYPTO_FALSE, fee);
}

static BRRlpItem
tezosFeeBasisRLPEncode (const BRTezosFeeBasis *feeBasis,
                        BRRlpCoder coder) {
    switch (feeBasis->type) {
        case FEE_BASIS_ESTIMATE:
            return rlpEncodeList (coder, 6,
                                  rlpEncodeUInt64 (coder, (uint64_t) feeBasis->type, 0),
                                  rlpEncodeUInt64 (coder, (uint64_t) feeBasis->u.estimate.mutezPerByte, 0),
                                  rlpEncodeUInt64 (coder, (uint64_t) feeBasis->u.estimate.sizeInBytes, 0),
                                  rlpEncodeUInt64 (coder, (uint64_t) feeBasis->u.estimate.gasLimit, 0),
                                  rlpEncodeUInt64 (coder, (uint64_t) feeBasis->u.estimate.storageLimit, 0),
                                  rlpEncodeUInt64 (coder, (uint64_t) feeBasis->u.estimate.counter, 0));

        case FEE_BASIS_ACTUAL:
            return rlpEncodeList2 (coder,
                                   rlpEncodeUInt64 (coder, feeBasis->type, 0),
                                   rlpEncodeUInt64 (coder, (uint64_t) feeBasis->u.actual.fee, 0));

        default:
            assert (0);
            return NULL;
    }
}

static BRTezosFeeBasis
tezosFeeBasisRLPDecode (BRRlpItem item,
                        BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (2 == itemsCount || 6 == itemsCount);

    BRTezosFeeBasisType type = (BRTezosFeeBasisType) rlpDecodeUInt64 (coder, items[0], 0);

    switch (type) {
        case FEE_BASIS_ESTIMATE:
            return tezosFeeBasisCreateEstimate ((BRTezosUnitMutez) rlpDecodeUInt64 (coder, items[1], 0),
                                                (size_t) rlpDecodeUInt64 (coder, items[2], 0),
                                                (int64_t) rlpDecodeUInt64 (coder, items[3], 0),
                                                (int64_t) rlpDecodeUInt64 (coder, items[4], 0),
                                                (int64_t) rlpDecodeUInt64 (coder, items[5], 0));

        case FEE_BASIS_ACTUAL:
            return tezosFeeBasisCreateActual ((BRTezosUnitMutez) rlpDecodeUInt64 (coder, items[1], 0));

        default:
            assert (0);
            return (BRTezosFeeBasis) {};
    }
}

static BRRlpItem
cryptoFeeBasisRLPEncodeXTZ (BRCryptoFeeBasis feeBasis,
                             BRCryptoNetwork network,
                             BRRlpCoder coder) {
    BRCryptoFeeBasisXTZ feeBasisXTZ = cryptoFeeBasisCoerceXTZ(feeBasis);

    return rlpEncodeList (coder, 3,
                          cryptoBlockChainTypeRLPEncode (feeBasis->type, coder),
                          cryptoNetworkRLPEncodeUnit (network, feeBasis->unit, coder),
                          tezosFeeBasisRLPEncode (&feeBasisXTZ->xtzFeeBasis, coder));
}

static BRCryptoFeeBasis
cryptoFeeBasisRLPDecodeXTZ (BRRlpItem item,
                             BRCryptoNetwork network,
                             BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (3 == itemsCount);

    BRCryptoBlockChainType type = cryptoBlockChainTypeRLPDecode (items[0], coder);
    assert (network->type == type);

    BRCryptoUnit unit = cryptoNetworkRLPDecodeUnit (network, items[1], coder);

    BRCryptoFeeBasisCreateContextXTZ contextXTZ = {
        tezosFeeBasisRLPDecode (items[2], coder)
    };

    BRCryptoFeeBasis feeBasis = cryptoFeeBasisAllocAndInit (sizeof (struct BRCryptoFeeBasisXTZRecord),
                                                            type,
                                                            unit,
                                                            &contextXTZ,
                                                            cryptoFeeBasisCreateCallbackXTZ);

    cryptoUnitGive (unit);

    return feeBasis;
}

static BRCryptoBoolean
cryptoFeeBasisIsEqualXTZ (BRCryptoFeeBasis feeBasis1, BRCryptoFeeBasis feeBasis2) {
    BRCryptoFeeBasisXTZ fb1 = cryptoFeeBasisCoerceXTZ (feeBasis1);
    BRCryptoFeeBasisXTZ fb2 = cryptoFeeBasisCoerceXTZ (feeBasis2);
    
    return AS_CRYPTO_BOOLEAN (tezosFeeBasisIsEqual (&fb1->xtzFeeBasis, &fb2->xtzFeeBasis));
}

// MARK: - Handlers

BRCryptoFeeBasisHandlers cryptoFeeBasisHandlersXTZ = {
    cryptoFeeBasisReleaseXTZ,
    cryptoFeeBasisGetCostFactorXTZ,
    cryptoFeeBasisGetPricePerCostFactorXTZ,
    cryptoFeeBasisGetFeeXTZ,
    cryptoFeeBasisRLPEncodeXTZ,
    cryptoFeeBasisRLPDecodeXTZ,
    cryptoFeeBasisIsEqualXTZ
};

