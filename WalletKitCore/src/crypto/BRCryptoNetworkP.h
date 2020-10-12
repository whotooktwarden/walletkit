//
//  BRCryptoNetworkP.h
//  BRCore
//
//  Created by Ed Gamble on 11/22/19.
//  Copyright © 2019 Breadwinner AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.

#ifndef BRCryptoNetworkP_h
#define BRCryptoNetworkP_h

#include <pthread.h>
#include <stdbool.h>

#include "support/rlp/BRRlp.h"
#include "support/BRArray.h"
#include "BRCryptoBaseP.h"
#include "BRCryptoHashP.h"
#include "BRCryptoNetwork.h"

#ifdef __cplusplus
extern "C" {
#endif

/// MARK: - Network Fee

struct BRCryptoNetworkFeeRecord {
    uint64_t confirmationTimeInMilliseconds;
    BRCryptoAmount pricePerCostFactor;
    BRCryptoUnit   pricePerCostFactorUnit;  // Until in BRCryptoAmount
    BRCryptoRef ref;
};

private_extern BRCryptoNetworkFee
cryptoNetworkFeeCreate (uint64_t confirmationTimeInMilliseconds,
                        BRCryptoAmount pricePerCostFactor,
                        BRCryptoUnit   pricePerCostFactorUnit);

/// MARK: - Currency Association

typedef struct {
    BRCryptoCurrency currency;
    BRCryptoUnit baseUnit;
    BRCryptoUnit defaultUnit;
    BRArrayOf(BRCryptoUnit) units;
} BRCryptoCurrencyAssociation;

/// MARK: - Network Handlers

typedef BRCryptoNetwork
(*BRCryptoNetworkCreateHandler) (BRCryptoNetworkListener listener,
                                 const char *uids,               // bitcoin-testnet
                                 const char *name,               // Bitcoin
                                 const char *network,            // testnet
                                 bool isMainnet,                 // false
                                 uint32_t confirmationPeriodInSeconds); // 10 * 60

typedef void
(*BRCryptoNetworkReleaseHandler) (BRCryptoNetwork network);

typedef BRCryptoAddress
(*BRCryptoNetworkCreateAddressHandler) (BRCryptoNetwork network,
                                        const char *addressAsString);

typedef BRCryptoBlockNumber
(*BRCryptoNetworkGetBlockNumberAtOrBeforeTimestampHandler) (BRCryptoNetwork network,
                                                            BRCryptoTimestamp timestamp);

typedef BRCryptoBoolean
(*BRCryptoNetworkIsAccountInitializedHandler) (BRCryptoNetwork network,
                                               BRCryptoAccount account);


typedef uint8_t *
(*BRCryptoNetworkGetAccountInitializationDataHandler) (BRCryptoNetwork network,
                                                       BRCryptoAccount account,
                                                       size_t *bytesCount);

typedef void
(*BRCryptoNetworkInitializeAccountHandler) (BRCryptoNetwork network,
                                            BRCryptoAccount account,
                                            const uint8_t *bytes,
                                            size_t bytesCount);

typedef BRCryptoHash
(*BRCryptoNetworkCreateHashFromStringHandler) (BRCryptoNetwork network,
                                               const char *string);

typedef char *
(*BRCryptoNetworkEncodeHashHandler) (BRCryptoHash hash);

typedef struct {
    BRCryptoNetworkCreateHandler create;
    BRCryptoNetworkReleaseHandler release;
    BRCryptoNetworkCreateAddressHandler createAddress;
    BRCryptoNetworkGetBlockNumberAtOrBeforeTimestampHandler getBlockNumberAtOrBeforeTimestamp;
    BRCryptoNetworkIsAccountInitializedHandler isAccountInitialized;
    BRCryptoNetworkGetAccountInitializationDataHandler getAccountInitializationData;
    BRCryptoNetworkInitializeAccountHandler initializeAccount;
    BRCryptoNetworkCreateHashFromStringHandler createHashFromString;
    BRCryptoNetworkEncodeHashHandler encodeHash;
} BRCryptoNetworkHandlers;

/// MARK: - Network

struct BRCryptoNetworkRecord {
    BRCryptoBlockChainType type;
    const BRCryptoNetworkHandlers *handlers;
    BRCryptoRef ref;
    size_t sizeInBytes;
    
    pthread_mutex_t lock;

    BRCryptoNetworkListener listener;
    
    char *uids;
    char *name;
    char *desc;
    bool isMainnet;

    BRCryptoBlockNumber height;
    BRCryptoHash verifiedBlockHash;

    // Base and associated currencies.
    BRCryptoCurrency currency;
    BRArrayOf(BRCryptoCurrencyAssociation) associations;

    uint32_t confirmationPeriodInSeconds;
    uint32_t confirmationsUntilFinal;

    // Address Schemes
    BRArrayOf(BRCryptoAddressScheme) addressSchemes;
    BRCryptoAddressScheme defaultAddressScheme;

    // Sync Modes
    BRArrayOf(BRCryptoSyncMode) syncModes;
    BRCryptoSyncMode defaultSyncMode;

    // Fees
    BRArrayOf(BRCryptoNetworkFee) fees;
};

typedef void *BRCryptoNetworkCreateContext;
typedef void (*BRCryptoNetworkCreateCallback) (BRCryptoNetworkCreateContext context,
                                               BRCryptoNetwork network);

extern BRCryptoNetwork
cryptoNetworkAllocAndInit (size_t sizeInBytes,
                           BRCryptoBlockChainType type,
                           BRCryptoNetworkListener listener,
                           const char *uids,
                           const char *name,
                           const char *desc,        // "mainnet", "testnet", "rinkeby"
                           bool isMainnet,
                           uint32_t confirmationPeriodInSeconds,
                           BRCryptoNetworkCreateContext createContext,
                           BRCryptoNetworkCreateCallback createCallback);

private_extern BRCryptoBlockChainType
cryptoNetworkGetType (BRCryptoNetwork network);

private_extern const char *
cryptoNetworkGetDesc (BRCryptoNetwork network);

private_extern uint32_t
cryptoNetworkGetConfirmationPeriodInSeconds (BRCryptoNetwork network);

private_extern void
cryptoNetworkAnnounce (BRCryptoNetwork network);

private_extern void
cryptoNetworkSetHeight (BRCryptoNetwork network,
                        BRCryptoBlockNumber height);

private_extern void
cryptoNetworkSetConfirmationsUntilFinal (BRCryptoNetwork network,
                                         uint32_t confirmationsUntilFinal);

private_extern void
cryptoNetworkSetCurrency (BRCryptoNetwork network,
                          BRCryptoCurrency currency);

private_extern void
cryptoNetworkAddCurrency (BRCryptoNetwork network,
                          BRCryptoCurrency currency,
                          BRCryptoUnit baseUnit,
                          BRCryptoUnit defaultUnit);

private_extern void
cryptoNetworkAddCurrencyUnit (BRCryptoNetwork network,
                              BRCryptoCurrency currency,
                              BRCryptoUnit unit);

private_extern void
cryptoNetworkAddNetworkFee (BRCryptoNetwork network,
                            BRCryptoNetworkFee fee);

private_extern void
cryptoNetworkSetNetworkFees (BRCryptoNetwork network,
                             const BRCryptoNetworkFee *fees,
                             size_t count);

private_extern BRCryptoBlockChainType
cryptoNetworkGetBlockChainType (BRCryptoNetwork network);

private_extern BRCryptoBlockNumber
cryptoNetworkGetBlockNumberAtOrBeforeTimestamp (BRCryptoNetwork network,
                                                BRCryptoTimestamp timestamp);

private_extern BRCryptoHash
cryptoNetworkCreateHashFromString (BRCryptoNetwork network,
                                   const char *string);

private_extern OwnershipGiven char *
cryptoNetworkEncodeHash (BRCryptoHash hash);

static inline void
cryptoNetworkGenerateEvent (BRCryptoNetwork network,
                            BRCryptoNetworkEvent event) {
    cryptoListenerGenerateNetworkEvent (&network->listener, network, event);
}

// MARK: - RLP Coding

private_extern BRRlpItem
cryptoBlockChainTypeRLPEncode (BRCryptoBlockChainType type,
                               BRRlpCoder coder);

private_extern BRCryptoBlockChainType
cryptoBlockChainTypeRLPDecode (BRRlpItem item,
                               BRRlpCoder coder);

private_extern BRRlpItem
cryptoNetworkRLPEncodeAddress (BRCryptoNetwork network,
                               BRCryptoAddress address,
                               BRRlpCoder coder);

private_extern BRCryptoAddress
cryptoNetworkRLPDecodeAddress (BRCryptoNetwork network,
                               BRRlpItem item,
                               BRRlpCoder coder);

private_extern BRRlpItem
cryptoNetworkRLPEncodeFeeBasis (BRCryptoNetwork network,
                                BRCryptoFeeBasis feeBasis,
                                BRRlpCoder coder);

private_extern BRCryptoFeeBasis
cryptoNetworkRLPDecodeFeeBasis (BRCryptoNetwork network,
                                BRRlpItem item,
                                BRRlpCoder coder);

private_extern BRRlpItem
cryptoNetworkRLPEncodeAmmount (BRCryptoNetwork network,
                               BRCryptoAmount amount,
                               BRRlpCoder coder);

private_extern BRCryptoAmount
cryptoNetworkRLPDecodeAmount (BRCryptoNetwork network,
                              BRRlpItem item,
                              BRRlpCoder coder);

private_extern BRRlpItem
cryptoNetworkRLPEncodeCurrency (BRCryptoNetwork network,
                                BRCryptoCurrency currency,
                                BRRlpCoder coder);

private_extern BRCryptoCurrency
cryptoNetworkRLPDecodeCurrency (BRCryptoNetwork network,
                                BRRlpItem item,
                                BRRlpCoder coder);

private_extern BRRlpItem
cryptoNetworkRLPEncodeUnit (BRCryptoNetwork network,
                            BRCryptoUnit unit,
                            BRRlpCoder coder);

private_extern BRCryptoUnit
cryptoNetworkRLPDecodeUnit (BRCryptoNetwork network,
                            BRRlpItem item,
                            BRRlpCoder coder);

#ifdef __cplusplus
}
#endif

#endif /* BRCryptoNetworkP_h */
