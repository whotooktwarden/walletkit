//
//  BRCryptoFeeBasisXRP.c
//  Core
//
//  Created by Ehsan Rezaie on 2020-09-04.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include "BRCryptoXRP.h"
#include "crypto/BRCryptoFeeBasisP.h"
#include "ripple/BRRipple.h"

static BRCryptoFeeBasisXRP
cryptoFeeBasisCoerce (BRCryptoFeeBasis feeBasis) {
    assert (CRYPTO_NETWORK_TYPE_XRP == feeBasis->type);
    return (BRCryptoFeeBasisXRP) feeBasis;
}

typedef struct {
    BRRippleFeeBasis xrpFeeBasis;
} BRCryptoFeeBasisCreateContextXRP;

static void
cryptoFeeBasisCreateCallbackXRP (BRCryptoFeeBasisCreateContext context,
                                 BRCryptoFeeBasis feeBasis) {
    BRCryptoFeeBasisCreateContextXRP *contextXRP = (BRCryptoFeeBasisCreateContextXRP*) context;
    BRCryptoFeeBasisXRP feeBasisXRP = cryptoFeeBasisCoerce (feeBasis);
    
    feeBasisXRP->xrpFeeBasis = contextXRP->xrpFeeBasis;
}

private_extern BRCryptoFeeBasis
cryptoFeeBasisCreateAsXRP (BRCryptoUnit unit,
                           BRRippleUnitDrops fee) {
    BRRippleFeeBasis xrpFeeBasis;
    xrpFeeBasis.costFactor = 1;
    xrpFeeBasis.pricePerCostFactor = fee;
    
    BRCryptoFeeBasisCreateContextXRP contextXRP = {
        xrpFeeBasis
    };
    
    return cryptoFeeBasisAllocAndInit (sizeof (struct BRCryptoFeeBasisXRPRecord),
                                       CRYPTO_NETWORK_TYPE_XRP,
                                       unit,
                                       &contextXRP,
                                       cryptoFeeBasisCreateCallbackXRP);
}

private_extern BRRippleFeeBasis
cryptoFeeBasisAsXRP (BRCryptoFeeBasis feeBasis) {
    BRCryptoFeeBasisXRP feeBasisXRP = cryptoFeeBasisCoerce (feeBasis);
    return feeBasisXRP->xrpFeeBasis;
}

static void
cryptoFeeBasisReleaseXRP (BRCryptoFeeBasis feeBasis) {
}

static double
cryptoFeeBasisGetCostFactorXRP (BRCryptoFeeBasis feeBasis) {
    return (double) cryptoFeeBasisCoerce (feeBasis)->xrpFeeBasis.costFactor;
}

static BRCryptoAmount
cryptoFeeBasisGetPricePerCostFactorXRP (BRCryptoFeeBasis feeBasis) {
    BRRippleFeeBasis xrpFeeBasis = cryptoFeeBasisCoerce (feeBasis)->xrpFeeBasis;
    return cryptoAmountCreateAsXRP (feeBasis->unit, CRYPTO_FALSE, xrpFeeBasis.pricePerCostFactor);
}

static BRCryptoAmount
cryptoFeeBasisGetFeeXRP (BRCryptoFeeBasis feeBasis) {
    return cryptoFeeBasisGetPricePerCostFactor (feeBasis);
}

static BRRlpItem
cryptoFeeBasisRLPEncodeXRP (BRCryptoFeeBasis feeBasis,
                             BRCryptoNetwork network,
                             BRRlpCoder coder) {
    BRCryptoFeeBasisXRP feeBasisXRP = cryptoFeeBasisCoerce(feeBasis);

    return rlpEncodeList (coder, 4,
                          cryptoBlockChainTypeRLPEncode (feeBasis->type, coder),
                          cryptoNetworkRLPEncodeUnit (network, feeBasis->unit, coder),
                          rlpEncodeUInt64 (coder, (uint64_t) feeBasisXRP->xrpFeeBasis.pricePerCostFactor, 0),
                          rlpEncodeUInt64 (coder, (uint64_t) feeBasisXRP->xrpFeeBasis.costFactor, 0));
}

static BRCryptoFeeBasis
cryptoFeeBasisRLPDecodeXRP (BRRlpItem item,
                             BRCryptoNetwork network,
                             BRRlpCoder coder) {
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList (coder, item, &itemsCount);
    assert (4 == itemsCount);

    BRCryptoBlockChainType type = cryptoBlockChainTypeRLPDecode (items[0], coder);
    assert (network->type == type);

    BRCryptoUnit unit = cryptoNetworkRLPDecodeUnit (network, items[1], coder);

    BRCryptoFeeBasisCreateContextXRP contextXRP = {
        ((BRRippleFeeBasis) {
            (BRRippleUnitDrops) rlpDecodeUInt64 (coder, items[2], 0),
            (uint32_t) rlpDecodeUInt64 (coder, items[3], 0)
        })
    };

    BRCryptoFeeBasis feeBasis = cryptoFeeBasisAllocAndInit (sizeof (struct BRCryptoFeeBasisXRPRecord),
                                                            type,
                                                            unit,
                                                            &contextXRP,
                                                            cryptoFeeBasisCreateCallbackXRP);

    cryptoUnitGive (unit);

    return feeBasis;
}

static BRCryptoBoolean
cryptoFeeBasisIsEqualXRP (BRCryptoFeeBasis feeBasis1, BRCryptoFeeBasis feeBasis2) {
    BRCryptoFeeBasisXRP fb1 = cryptoFeeBasisCoerce (feeBasis1);
    BRCryptoFeeBasisXRP fb2 = cryptoFeeBasisCoerce (feeBasis2);

    return rippleFeeBasisIsEqual (&fb1->xrpFeeBasis, &fb2->xrpFeeBasis);
}

// MARK: - Handlers

BRCryptoFeeBasisHandlers cryptoFeeBasisHandlersXRP = {
    cryptoFeeBasisReleaseXRP,
    cryptoFeeBasisGetCostFactorXRP,
    cryptoFeeBasisGetPricePerCostFactorXRP,
    cryptoFeeBasisGetFeeXRP,
    cryptoFeeBasisRLPEncodeXRP,
    cryptoFeeBasisRLPDecodeXRP,
    cryptoFeeBasisIsEqualXRP
};
