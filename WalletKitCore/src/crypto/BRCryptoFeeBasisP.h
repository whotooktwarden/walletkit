//
//  BRCryptoFeeBasisP.h
//  BRCore
//
//  Created by Ed Gamble on 11/22/19.
//  Copyright © 2019 Breadwinner AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.

#ifndef BRCryptoFeeBasisP_h
#define BRCryptoFeeBasisP_h

#include "BRCryptoFeeBasis.h"
#include "BRCryptoBaseP.h"
#include "support/rlp/BRRlp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void
(*BRCryptoFeeBasisReleaseHandler) (BRCryptoFeeBasis feeBasis);

typedef double
(*BRCryptoFeeBasisGetCostFactorHandler) (BRCryptoFeeBasis feeBasis);

typedef BRCryptoAmount
(*BRCryptoFeeBasisGetPricePerCostFactorHandler) (BRCryptoFeeBasis feeBasis);

typedef BRCryptoAmount
(*BRCryptoFeeBasisGetFeeHandler) (BRCryptoFeeBasis feeBasis);

typedef BRRlpItem
(*BRCryptoFeeBasisRLPEncodeHandler) (BRCryptoFeeBasis feeBasis,
                                     BRCryptoNetwork network,
                                     BRRlpCoder coder);

typedef BRCryptoFeeBasis
(*BRCryptoFeeBasisRLPDecodeHandler) (BRRlpItem item,
                                     BRCryptoNetwork network,
                                     BRRlpCoder coder);

typedef BRCryptoBoolean
(*BRCryptoFeeBasisIsEqualHandler) (BRCryptoFeeBasis feeBasis1,
                                   BRCryptoFeeBasis feeBasis2);

typedef struct {
    BRCryptoFeeBasisReleaseHandler release;
    BRCryptoFeeBasisGetCostFactorHandler getCostFactor;
    BRCryptoFeeBasisGetPricePerCostFactorHandler getPricePerCostFactor;
    BRCryptoFeeBasisGetFeeHandler getFee;
    BRCryptoFeeBasisRLPEncodeHandler encodeRLP;
    BRCryptoFeeBasisRLPDecodeHandler decodeRLP;
    BRCryptoFeeBasisIsEqualHandler isEqual;
} BRCryptoFeeBasisHandlers;

struct BRCryptoFeeBasisRecord {
    BRCryptoBlockChainType type;
    const BRCryptoFeeBasisHandlers *handlers;
    BRCryptoRef ref;
    size_t sizeInBytes;
    
    BRCryptoUnit unit;
};

typedef void *BRCryptoFeeBasisCreateContext;
typedef void (*BRCryptoFeeBasisCreateCallback) (BRCryptoFeeBasisCreateContext context,
                                                BRCryptoFeeBasis feeBasis);

private_extern BRCryptoFeeBasis
cryptoFeeBasisAllocAndInit (size_t sizeInBytes,
                            BRCryptoBlockChainType type,
                            BRCryptoUnit unit,
                            BRCryptoFeeBasisCreateContext  createContext,
                            BRCryptoFeeBasisCreateCallback createCallback);

private_extern BRCryptoBlockChainType
cryptoFeeBasisGetType (BRCryptoFeeBasis feeBasis);

private_extern BRCryptoUnit
cryptoFeeBasisGetUnit (BRCryptoFeeBasis feeBasis);

#ifdef __cplusplus
}
#endif

#endif /* BRCryptoFeeBasisP_h */
