//
//  BRCryptoWalletManagerXRP.c
//  Core
//
//  Created by Ehsan Rezaie on 2020-05-19.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include "BRCryptoXRP.h"

#include "crypto/BRCryptoAccountP.h"
#include "crypto/BRCryptoNetworkP.h"
#include "crypto/BRCryptoKeyP.h"
#include "crypto/BRCryptoClientP.h"
#include "crypto/BRCryptoWalletP.h"
#include "crypto/BRCryptoAmountP.h"
#include "crypto/BRCryptoWalletManagerP.h"
#include "crypto/BRCryptoFileService.h"

#include "ripple/BRRippleAccount.h"

// MARK: - Transfer File Service

enum {
    CRYPTO_FILE_SERVICE_TRANSFER_VERSION_1_XRP
};

#define CRYPTO_FILE_SERVICE_TRANSFER_VERSION_1  \
  cryptoFileServiceTransferVersionCreate (CRYPTO_FILE_SERVICE_TRANSFER_BASE_VERSION_1, \
                                          CRYPTO_FILE_SERVICE_TRANSFER_VERSION_1_XRP)

static BRFileServiceTypeSpecification fileServiceSpecifications[] = {
    {
        fileServiceTypeTransfers,
        CRYPTO_FILE_SERVICE_TRANSFER_VERSION_1,
        1,
        {
            {
                CRYPTO_FILE_SERVICE_TRANSFER_VERSION_1,
                fileServiceTypeTransferV1Identifier,
                fileServiceTypeTransferV1Reader,
                fileServiceTypeTransferV1Writer
            }
        }
    }
};

static size_t fileServiceSpecificationsCount = sizeof(fileServiceSpecifications)/sizeof(BRFileServiceTypeSpecification);

// MARK: - Events

//TODO:XRP make common
const BREventType *xrpEventTypes[] = {
    // ...
};

const unsigned int
xrpEventTypesCount = (sizeof (xrpEventTypes) / sizeof (BREventType*));


//static BRCryptoWalletManagerXRP
//cryptoWalletManagerCoerce (BRCryptoWalletManager wm) {
//    assert (CRYPTO_NETWORK_TYPE_XRP == wm->type);
//    return (BRCryptoWalletManagerXRP) wm;
//}

// MARK: - Handlers

static BRCryptoWalletManager
cryptoWalletManagerCreateXRP (BRCryptoWalletManagerListener listener,
                              BRCryptoClient client,
                              BRCryptoAccount account,
                              BRCryptoNetwork network,
                              BRCryptoSyncMode mode,
                              BRCryptoAddressScheme scheme,
                              const char *path) {
    BRCryptoWalletManager manager = cryptoWalletManagerAllocAndInit (sizeof (struct BRCryptoWalletManagerXRPRecord),
                                                                     cryptoNetworkGetType(network),
                                                                     listener,
                                                                     client,
                                                                     account,
                                                                     network,
                                                                     scheme,
                                                                     path,
                                                                     CRYPTO_CLIENT_REQUEST_USE_TRANSFERS,
                                                                     NULL,
                                                                     NULL);

    pthread_mutex_unlock (&manager->lock);
    return manager;
}

static void
cryptoWalletManagerReleaseXRP (BRCryptoWalletManager manager) {
}

static BRFileService
crytpWalletManagerCreateFileServiceXRP (BRCryptoWalletManager manager,
                                        const char *basePath,
                                        const char *currency,
                                        const char *network,
                                        BRFileServiceContext context,
                                        BRFileServiceErrorHandler handler) {
    return fileServiceCreateFromTypeSpecfications (basePath, currency, network,
                                                   context, handler,
                                                   fileServiceSpecificationsCount,
                                                   fileServiceSpecifications);
}

static const BREventType **
cryptoWalletManagerGetEventTypesXRP (BRCryptoWalletManager manager,
                                     size_t *eventTypesCount) {
    assert (NULL != eventTypesCount);
    *eventTypesCount = xrpEventTypesCount;
    return xrpEventTypes;
}

static BRCryptoClientP2PManager
crytpWalletManagerCreateP2PManagerXRP (BRCryptoWalletManager manager) {
    // not supported
    return NULL;
}

static BRCryptoBoolean
cryptoWalletManagerSignTransactionWithSeedXRP (BRCryptoWalletManager manager,
                                               BRCryptoWallet wallet,
                                               BRCryptoTransfer transfer,
                                               UInt512 seed) {
    BRRippleAccount account = cryptoAccountAsXRP (manager->account);
    BRRippleTransaction tid = cryptoTransferCoerceXRP(transfer)->xrpTransaction;
    if (tid) {
        size_t tx_size = rippleAccountSignTransaction (account, tid, seed);
        return AS_CRYPTO_BOOLEAN(tx_size > 0);
    } else {
        return CRYPTO_FALSE;
    }
}

static BRCryptoBoolean
cryptoWalletManagerSignTransactionWithKeyXRP (BRCryptoWalletManager manager,
                                              BRCryptoWallet wallet,
                                              BRCryptoTransfer transfer,
                                              BRCryptoKey key) {
    assert(0);
    return CRYPTO_FALSE;
}

static BRCryptoAmount
cryptoWalletManagerEstimateLimitXRP (BRCryptoWalletManager manager,
                                     BRCryptoWallet  wallet,
                                     BRCryptoBoolean asMaximum,
                                     BRCryptoAddress target,
                                     BRCryptoNetworkFee networkFee,
                                     BRCryptoBoolean *needEstimate,
                                     BRCryptoBoolean *isZeroIfInsuffientFunds,
                                     BRCryptoUnit unit) {
    UInt256 amount = UINT256_ZERO;
    
    *needEstimate = CRYPTO_FALSE;
    *isZeroIfInsuffientFunds = CRYPTO_FALSE;
    
    if (CRYPTO_TRUE == asMaximum) {
        BRCryptoAmount minBalance = wallet->balanceMinimum;
        assert(minBalance);
        
        // Available balance based on minimum wallet balance
        BRCryptoAmount balance = cryptoAmountSub(wallet->balance, minBalance);
        
        // Ripple has fixed network fee (costFactor = 1.0)
        BRCryptoAmount fee = cryptoNetworkFeeGetPricePerCostFactor (networkFee);
        BRCryptoAmount newBalance = cryptoAmountSub(balance, fee);
        
        if (CRYPTO_TRUE == cryptoAmountIsNegative(newBalance)) {
            amount = UINT256_ZERO;
        } else {
            amount = cryptoAmountGetValue(newBalance);
        }
        
        cryptoAmountGive (balance);
        cryptoAmountGive (fee);
        cryptoAmountGive (newBalance);
    }
    
    return cryptoAmountCreateInternal (unit,
                                       CRYPTO_FALSE,
                                       amount,
                                       0);
}

static BRCryptoFeeBasis
cryptoWalletManagerEstimateFeeBasisXRP (BRCryptoWalletManager manager,
                                        BRCryptoWallet wallet,
                                        BRCryptoCookie cookie,
                                        BRCryptoAddress target,
                                        BRCryptoAmount amount,
                                        BRCryptoNetworkFee networkFee,
                                        size_t attributesCount,
                                        OwnershipKept BRCryptoTransferAttribute *attributes) {
    UInt256 value = cryptoAmountGetValue (cryptoNetworkFeeGetPricePerCostFactor (networkFee));
    BRRippleUnitDrops fee = value.u64[0];
    return cryptoFeeBasisCreateAsXRP (wallet->unitForFee, fee);
}

static void
cryptoWalletManagerRecoverTransfersFromTransactionBundleXRP (BRCryptoWalletManager manager,
                                                             OwnershipKept BRCryptoClientTransactionBundle bundle) {
    // Not XRP functionality
    assert (0);
}

static void
cryptoWalletManagerRecoverTransferFromTransferBundleXRP (BRCryptoWalletManager manager,
                                                         OwnershipKept BRCryptoClientTransferBundle bundle) {
    // create BRRippleTransfer

    BRRippleAccount xrpAccount = cryptoAccountAsXRP(manager->account);
    
    BRRippleUnitDrops amountDrops = 0;
    sscanf(bundle->amount, "%" PRIu64, &amountDrops);

    BRRippleUnitDrops feeDrops = 0;
    if (NULL != bundle->fee) sscanf(bundle->fee, "%" PRIu64, &feeDrops);
    BRRippleFeeBasis xrpFeeBasis = { feeDrops, 1};

    BRRippleAddress toAddress   = rippleAddressCreateFromString (bundle->to,   false);
    BRRippleAddress fromAddress = rippleAddressCreateFromString (bundle->from, false);
    // Convert the hash string to bytes
    BRRippleTransactionHash txId;
    hexDecode(txId.bytes, sizeof(txId.bytes), bundle->hash, strlen(bundle->hash));
    int error = (CRYPTO_TRANSFER_STATE_ERRORED == bundle->status);

    bool xrpTransactionNeedFree = true;
    BRRippleTransaction xrpTransaction = rippleTransactionCreateFull (fromAddress,
                                                                      toAddress,
                                                                      amountDrops,
                                                                      xrpFeeBasis,
                                                                      txId,
                                                                      bundle->blockTimestamp,
                                                                      bundle->blockNumber,
                                                                      error);

    rippleAddressFree (toAddress);
    rippleAddressFree (fromAddress);

    // create BRCryptoTransfer
    
    BRCryptoWallet wallet = cryptoWalletManagerGetWallet (manager);
    BRCryptoHash hash = cryptoHashCreateAsXRP (txId);
    
    BRCryptoTransfer baseTransfer = cryptoWalletGetTransferByHash (wallet, hash);
    cryptoHashGive (hash);

    BRCryptoFeeBasis      feeBasis = cryptoFeeBasisCreateAsXRP (wallet->unitForFee, feeDrops);
    BRCryptoTransferState state    = cryptoClientTransferBundleGetTransferState (bundle, feeBasis);

    if (NULL == baseTransfer) {
        baseTransfer = cryptoTransferCreateAsXRP (wallet->listenerTransfer,
                                                  wallet->unit,
                                                  wallet->unitForFee,
                                                  state,
                                                  xrpAccount,
                                                  xrpTransaction);
        xrpTransactionNeedFree = false;

        cryptoWalletAddTransfer (wallet, baseTransfer);
    }
    else {
        cryptoTransferSetState (baseTransfer, state);
    }

    cryptoWalletManagerRecoverTransferAttributesFromTransferBundle (wallet, baseTransfer, bundle);

    cryptoFeeBasisGive (feeBasis);
    cryptoTransferStateRelease (&state);

    //TODO:XRP save to fileService

    if (xrpTransactionNeedFree)
        rippleTransactionFree (xrpTransaction);
}

extern BRCryptoWalletSweeperStatus
cryptoWalletManagerWalletSweeperValidateSupportedXRP (BRCryptoWalletManager manager,
                                                      BRCryptoWallet wallet,
                                                      BRCryptoKey key) {
    return CRYPTO_WALLET_SWEEPER_UNSUPPORTED_CURRENCY;
}

extern BRCryptoWalletSweeper
cryptoWalletManagerCreateWalletSweeperXRP (BRCryptoWalletManager manager,
                                           BRCryptoWallet wallet,
                                           BRCryptoKey key) {
    // not supported
    return NULL;
}

static BRCryptoWallet
cryptoWalletManagerCreateWalletXRP (BRCryptoWalletManager manager,
                                    BRCryptoCurrency currency) {
    BRRippleAccount xrpAccount = cryptoAccountAsXRP(manager->account);

    // Create the primary BRCryptoWallet
    BRCryptoNetwork  network       = manager->network;
    BRCryptoUnit     unitAsBase    = cryptoNetworkGetUnitAsBase    (network, currency);
    BRCryptoUnit     unitAsDefault = cryptoNetworkGetUnitAsDefault (network, currency);

    BRCryptoWalletFileServiceContext fileServiceContext = {
        manager->fileService,
        fileServiceTypeTransfers
    };

    BRCryptoWallet wallet = cryptoWalletCreateAsXRP (manager->listenerWallet,
                                                     fileServiceContext,
                                                     unitAsDefault,
                                                     unitAsDefault,
                                                     xrpAccount);
    cryptoWalletManagerAddWallet (manager, wallet);
    
    BRArrayOf(BRCryptoTransfer) transfers = initialTransfersLoad (manager);
    if (NULL == transfers) array_new (transfers, 1);

    for (size_t index = 0; index < array_count(transfers); index++) {
        cryptoTransferSetListener (transfers[index], wallet->listenerTransfer);
        cryptoWalletAddTransfer (wallet, transfers[index]);
    }
    array_free_all (transfers, cryptoTransferGive);

    cryptoUnitGive (unitAsDefault);
    cryptoUnitGive (unitAsBase);
    
    return wallet;
}

BRCryptoWalletManagerHandlers cryptoWalletManagerHandlersXRP = {
    cryptoWalletManagerCreateXRP,
    cryptoWalletManagerReleaseXRP,
    crytpWalletManagerCreateFileServiceXRP,
    cryptoWalletManagerGetEventTypesXRP,
    crytpWalletManagerCreateP2PManagerXRP,
    cryptoWalletManagerCreateWalletXRP,
    cryptoWalletManagerSignTransactionWithSeedXRP,
    cryptoWalletManagerSignTransactionWithKeyXRP,
    cryptoWalletManagerEstimateLimitXRP,
    cryptoWalletManagerEstimateFeeBasisXRP,
    cryptoWalletManagerRecoverTransfersFromTransactionBundleXRP,
    cryptoWalletManagerRecoverTransferFromTransferBundleXRP,
    NULL,//BRCryptoWalletManagerRecoverFeeBasisFromFeeEstimateHandler not supported
    cryptoWalletManagerWalletSweeperValidateSupportedXRP,
    cryptoWalletManagerCreateWalletSweeperXRP
};
