//
//  BRCryptoWalletManager.c
//  BRCore
//
//  Created by Ed Gamble on 3/19/19.
//  Copyright © 2019 breadwallet. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.

#include <assert.h>
#include <arpa/inet.h>      // struct in_addr
#include <ctype.h>          // toupper()

#include "BRCryptoBase.h"

#include "BRCryptoKeyP.h"
#include "BRCryptoAccountP.h"
#include "BRCryptoNetworkP.h"
#include "BRCryptoAddressP.h"
#include "BRCryptoAmountP.h"
#include "BRCryptoFeeBasisP.h"
#include "BRCryptoTransferP.h"
#include "BRCryptoWalletP.h"
#include "BRCryptoPaymentP.h"
#include "BRCryptoClientP.h"

#include "BRCryptoWalletManager.h"
#include "BRCryptoWalletManagerP.h"

#include "BRCryptoHandlersP.h"

#include "bitcoin/BRMerkleBlock.h"
#include "bitcoin/BRPeer.h"
#include "support/BRFileService.h"
#include "support/event/BREventAlarm.h"

// We'll do a period QRY 'tick-tock' CWM_CONFIRMATION_PERIOD_FACTOR times in
// each network's ConfirmationPeriod.  Thus, for example, the Bitcoin confirmation period is
// targeted for every 10 minutes; we'll check every 2.5 minutes.
#define CWM_CONFIRMATION_PERIOD_FACTOR  (4)

uint64_t BLOCK_HEIGHT_UNBOUND_VALUE = UINT64_MAX;

static void
cryptoWalletManagerPeriodicDispatcher (BREventHandler handler,
                                       BREventTimeout *event);

static void
cryptoWalletManagerFileServiceErrorHandler (BRFileServiceContext context,
                                            BRFileService fs,
                                            BRFileServiceError error);

IMPLEMENT_CRYPTO_GIVE_TAKE (BRCryptoWalletManager, cryptoWalletManager)

/// =============================================================================================
///
/// MARK: - Wallet Manager State
///
///
private_extern BRCryptoWalletManagerState
cryptoWalletManagerStateInit(BRCryptoWalletManagerStateType type) {
    switch (type) {
        case CRYPTO_WALLET_MANAGER_STATE_CREATED:
        case CRYPTO_WALLET_MANAGER_STATE_CONNECTED:
        case CRYPTO_WALLET_MANAGER_STATE_SYNCING:
        case CRYPTO_WALLET_MANAGER_STATE_DELETED:
            return (BRCryptoWalletManagerState) { type };
        case CRYPTO_WALLET_MANAGER_STATE_DISCONNECTED:
            assert (0); // if you are hitting this, use cryptoWalletManagerStateDisconnectedInit!
            return (BRCryptoWalletManagerState) {
                CRYPTO_WALLET_MANAGER_STATE_DISCONNECTED,
                { .disconnected = { cryptoWalletManagerDisconnectReasonUnknown() } }
            };
    }
}

private_extern BRCryptoWalletManagerState
cryptoWalletManagerStateDisconnectedInit(BRCryptoWalletManagerDisconnectReason reason) {
    return (BRCryptoWalletManagerState) {
        CRYPTO_WALLET_MANAGER_STATE_DISCONNECTED,
        { .disconnected = { reason } }
    };
}

/// =============================================================================================
///
/// MARK: - Wallet Manager
///
///

#pragma clang diagnostic push
#pragma GCC diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-function"
static BRArrayOf(BRCryptoCurrency)
cryptoWalletManagerGetCurrenciesOfIntereest (BRCryptoWalletManager cwm) {
    BRArrayOf(BRCryptoCurrency) currencies;

    array_new (currencies, 3);
    return currencies;
}

static void
cryptoWalletManagerReleaseCurrenciesOfIntereest (BRCryptoWalletManager cwm,
                                                 BRArrayOf(BRCryptoCurrency) currencies) {
    for (size_t index = 0; index < array_count(currencies); index++)
        cryptoCurrencyGive (currencies[index]);
    array_free (currencies);
}
#pragma clang diagnostic pop
#pragma GCC diagnostic pop

//static void
//cryptoWalletMangerSignalWalletCreated (BRCryptoWalletManager manager,
//                                       BRCryptoWallet wallet) {
//    cryptoWalletManagerGenerateWalletEvent (manager, wallet, (BRCryptoWalletEvent) {
//        CRYPTO_WALLET_EVENT_CREATED
//    });
//
//    cryptoWalletManagerGenerateManagerEvent(manager, (BRCryptoWalletManagerEvent) {
//        CRYPTO_WALLET_MANAGER_EVENT_WALLET_ADDED,
//        { .wallet = { cryptoWalletTake (wallet) }}
//    });
//
//    BRCryptoAmount balance = cryptoWalletGetBalance (wallet);
//
//    cryptoWalletManagerGenerateWalletEvent (manager, wallet, (BRCryptoWalletEvent) {
//        CRYPTO_WALLET_EVENT_BALANCE_UPDATED,
//        { .balanceUpdated = { balance }}
//    });
//}

extern BRCryptoWalletManager
cryptoWalletManagerAllocAndInit (size_t sizeInBytes,
                                 BRCryptoBlockChainType type,
                                 BRCryptoWalletManagerListener listener,
                                 BRCryptoClient client,
                                 BRCryptoAccount account,
                                 BRCryptoNetwork network,
                                 BRCryptoAddressScheme scheme,
                                 const char *path,
                                 BRCryptoClientQRYByType byType,
                                 BRCryptoWalletManagerCreateContext createContext,
                                 BRCryptoWalletManagerCreateCallback createCallback) {
    assert (sizeInBytes >= sizeof (struct BRCryptoWalletManagerRecord));
    assert (type == cryptoNetworkGetType(network));

    BRCryptoWalletManager manager = calloc (1, sizeInBytes);
    if (NULL == manager) return NULL;

    manager->type = type;
    manager->handlers = cryptoHandlersLookup(type)->manager;
    network->sizeInBytes = sizeInBytes;
    manager->listener = listener;

    manager->client  = client;
    manager->network = cryptoNetworkTake (network);
    manager->account = cryptoAccountTake (account);
    manager->state   = cryptoWalletManagerStateInit (CRYPTO_WALLET_MANAGER_STATE_CREATED);
    manager->addressScheme = scheme;
    manager->path = strdup (path);

    manager->byType = byType;
    
    // File Service
    const char *currencyName = cryptoBlockChainTypeGetCurrencyCode (manager->type);
    const char *networkName  = cryptoNetworkGetDesc(network);

    // TODO: Replace `createFileService` with `getFileServiceSpecifications`
    manager->fileService = manager->handlers->createFileService (manager,
                                                         manager->path,
                                                         currencyName,
                                                         networkName,
                                                         manager,
                                                         cryptoWalletManagerFileServiceErrorHandler);

    // Create the alarm clock, but don't start it.
    alarmClockCreateIfNecessary(0);

    // Create the event handler name (useful for debugging).
    char handlerName[5 + strlen(currencyName) + 1];
    sprintf(handlerName, "Core %s", currencyName);
    for (char *s = &handlerName[5]; *s; s++) *s = toupper (*s);

    // Get the event handler types.
    size_t eventTypesCount;
    const BREventType **eventTypes = manager->handlers->getEventTypes (manager, &eventTypesCount);

    // Create the event handler
    manager->handler = eventHandlerCreate (handlerName,
                                           eventTypes,
                                           eventTypesCount,
                                           &manager->lock);

    eventHandlerSetTimeoutDispatcher (manager->handler,
                                      (1000 * cryptoNetworkGetConfirmationPeriodInSeconds(network)) / CWM_CONFIRMATION_PERIOD_FACTOR,
                                      (BREventDispatcher) cryptoWalletManagerPeriodicDispatcher,
                                      (void*) manager);

//    manager->listenerTrampoline = (BRCryptoListener) {
//        listener.context,
//        cryptoWalletManagerListenerCallbackTrampoline,
//        cryptoWalletListenerCallbackTrampoline,
//        cryptoTransferListenerCallbackTrampoline
//    };

    manager->listenerWallet = cryptoListenerCreateWalletListener (&manager->listener, manager);

    // Setup `wallet` and `wallets.
    manager->wallet = NULL;
    array_new (manager->wallets, 1);

    manager->ref = CRYPTO_REF_ASSIGN (cryptoWalletManagerRelease);
    pthread_mutex_init_brd (&manager->lock, PTHREAD_MUTEX_RECURSIVE);

    BRCryptoTimestamp   earliestAccountTime = cryptoAccountGetTimestamp (account);
    BRCryptoBlockNumber earliestBlockNumber = cryptoNetworkGetBlockNumberAtOrBeforeTimestamp(network, earliestAccountTime);
    BRCryptoBlockNumber latestBlockNumber   = cryptoNetworkGetHeight (network);

    // Setup the QRY Manager
    manager->qryManager = cryptoClientQRYManagerCreate (client,
                                                        manager,
                                                        manager->byType,
                                                        earliestBlockNumber,
                                                        latestBlockNumber);

    // Setup the P2P Manager.
    manager->p2pManager = NULL; // manager->handlers->createP2PManager (manager);

    if (createCallback) createCallback (createContext, manager);
    
    // Announce the created manager; this must preceed any wallet created/added events
    cryptoWalletManagerGenerateEvent (manager, (BRCryptoWalletManagerEvent) {
        CRYPTO_WALLET_MANAGER_EVENT_CREATED
    });

    // Create the primary wallet
    manager->wallet = cryptoWalletManagerCreateWallet (manager, network->currency);

    // Create the P2P manager
    manager->p2pManager = manager->handlers->createP2PManager (manager);

    pthread_mutex_lock (&manager->lock);
    return manager;
}

extern BRCryptoWalletManager
cryptoWalletManagerCreate (BRCryptoWalletManagerListener listener,
                           BRCryptoClient client,
                           BRCryptoAccount account,
                           BRCryptoNetwork network,
                           BRCryptoSyncMode mode,
                           BRCryptoAddressScheme scheme,
                           const char *path) {
    // Only create a wallet manager for accounts that are initializedon network.
    if (CRYPTO_FALSE == cryptoNetworkIsAccountInitialized (network, account))
        return NULL;

    // Lookup the handler for the network's type.
    BRCryptoBlockChainType type = cryptoNetworkGetType(network);
    const BRCryptoWalletManagerHandlers *handlers = cryptoHandlersLookup(type)->manager;

    // Create the manager
    BRCryptoWalletManager manager = handlers->create (listener,
                                                      client,
                                                      account,
                                                      network,
                                                      mode,
                                                      scheme,
                                                      path);
    if (NULL == manager) return NULL;

    // Set the mode for QRY or P2P syncing
    cryptoWalletManagerSetMode (manager, mode);

    // Start the event handler.
    cryptoWalletManagerStart (manager);

    return manager;
}

#define _peer_log_x printf
static void
cryptoWalletManagerFileServiceErrorHandler (BRFileServiceContext context,
                                            BRFileService fs,
                                            BRFileServiceError error) {
    switch (error.type) {
        case FILE_SERVICE_IMPL:
            // This actually a FATAL - an unresolvable coding error.
            _peer_log_x ("CRY: FileService Error: IMPL: %s\n", error.u.impl.reason);
            break;
        case FILE_SERVICE_UNIX:
            _peer_log_x ("CRY: FileService Error: UNIX: %s\n", strerror(error.u.unx.error));
            break;
        case FILE_SERVICE_ENTITY:
            // This is likely a coding error too.
            _peer_log_x ("CRY: FileService Error: ENTITY (%s): %s\n",
                     error.u.entity.type,
                     error.u.entity.reason);
            break;
        case FILE_SERVICE_SDB:
            _peer_log_x ("CRY: FileService Error: SDB: (%d): %s\n",
                       error.u.sdb.code,
                       error.u.sdb.reason);
            break;
    }
    _peer_log_x ("CRY: FileService Error: FORCED SYNC%s\n", "");

    // BRWalletManager bwm = (BRWalletManager) context;
    // TODO(fix): What do we actually want to happen here?
    // if (NULL != bwm->peerManager)
    //     BRPeerManagerRescan (bwm->peerManager);
}
#undef _peer_log_x


static void
cryptoWalletManagerRelease (BRCryptoWalletManager cwm) {
    pthread_mutex_lock (&cwm->lock);

    // Ensure CWM is stopped...
    cryptoWalletManagerStop (cwm);

    // ... then release any type-specific resources
    cwm->handlers->release (cwm);

    // ... then release memory.
    cryptoAccountGive (cwm->account);
    cryptoNetworkGive (cwm->network);
    if (NULL != cwm->wallet) cryptoWalletGive (cwm->wallet);

    // .. then give all the wallets
    for (size_t index = 0; index < array_count(cwm->wallets); index++)
        cryptoWalletGive (cwm->wallets[index]);
    array_free (cwm->wallets);

    // ... then the p2p and qry managers
    if (NULL != cwm->p2pManager) cryptoClientP2PManagerRelease(cwm->p2pManager);
    cryptoClientQRYManagerRelease (cwm->qryManager);

    // ... then the fileService
    fileServiceRelease (cwm->fileService);

    // ... then the eventHandler
    eventHandlerDestroy (cwm->handler);
//    eventHandlerDestroy (cwm->listenerHandler);

    // ... and finally individual memory allocations
    free (cwm->path);

    pthread_mutex_unlock  (&cwm->lock);
    pthread_mutex_destroy (&cwm->lock);

    memset (cwm, 0, sizeof(*cwm));
    free (cwm);
}

extern BRCryptoNetwork
cryptoWalletManagerGetNetwork (BRCryptoWalletManager cwm) {
    return cryptoNetworkTake (cwm->network);
}

extern BRCryptoBoolean
cryptoWalletManagerHasNetwork (BRCryptoWalletManager cwm,
                               BRCryptoNetwork network) {
    return AS_CRYPTO_BOOLEAN (cwm->network == network);
}

extern BRCryptoAccount
cryptoWalletManagerGetAccount (BRCryptoWalletManager cwm) {
    return cryptoAccountTake (cwm->account);
}

extern BRCryptoBoolean
cryptoWalletManagerHasAccount (BRCryptoWalletManager cwm,
                               BRCryptoAccount account) {
    return AS_CRYPTO_BOOLEAN (cwm->account == account);
}

extern void
cryptoWalletManagerSetMode (BRCryptoWalletManager cwm, BRCryptoSyncMode mode) {
    pthread_mutex_lock (&cwm->lock);

    // Get default p2p{Sync,Send} Managers
    BRCryptoClientSync p2pSync = (NULL != cwm->p2pManager
                                  ? cryptoClientP2PManagerAsSync (cwm->p2pManager)
                                  : cryptoClientQRYManagerAsSync (cwm->qryManager));
    BRCryptoClientSend p2pSend = (NULL != cwm->p2pManager
                                  ? cryptoClientP2PManagerAsSend (cwm->p2pManager)
                                  : cryptoClientQRYManagerAsSend (cwm->qryManager));

    BRCryptoClientSync qrySync = cryptoClientQRYManagerAsSync (cwm->qryManager);
    BRCryptoClientSend qrySend = cryptoClientQRYManagerAsSend (cwm->qryManager);

    // TODO: Manager Memory?

    // Set cwm->can{Sync,Send} based on mode.
    switch (mode) {
        case CRYPTO_SYNC_MODE_API_ONLY:
            cwm->canSync = qrySync;
            cwm->canSend = qrySend;
            break;
        case CRYPTO_SYNC_MODE_API_WITH_P2P_SEND:
            cwm->canSync = qrySync;
            cwm->canSend = p2pSend;
            break;
        case CRYPTO_SYNC_MODE_P2P_WITH_API_SYNC:
             // Initial sync w/ QRY, thereafter w/ P2P
            cwm->canSync = qrySync;
            cwm->canSend = p2pSend;
            break;
        case CRYPTO_SYNC_MODE_P2P_ONLY:
            cwm->canSync = p2pSync;
            cwm->canSend = p2pSend;
            break;
    }
    cwm->syncMode = mode;
    pthread_mutex_unlock (&cwm->lock);
}

extern BRCryptoSyncMode
cryptoWalletManagerGetMode (BRCryptoWalletManager cwm) {
    pthread_mutex_lock (&cwm->lock);
    BRCryptoSyncMode mode = cwm->syncMode;
    pthread_mutex_unlock (&cwm->lock);
    return mode;
}

extern BRCryptoWalletManagerState
cryptoWalletManagerGetState (BRCryptoWalletManager cwm) {
    pthread_mutex_lock (&cwm->lock);
    BRCryptoWalletManagerState state = cwm->state;
    pthread_mutex_unlock (&cwm->lock);
    return state;
}

private_extern void
cryptoWalletManagerSetState (BRCryptoWalletManager cwm,
                             BRCryptoWalletManagerState newState) {
    pthread_mutex_lock (&cwm->lock);
    BRCryptoWalletManagerState oldState = cwm->state;
    cwm->state = newState;
    pthread_mutex_unlock (&cwm->lock);

    if (oldState.type != newState.type)
        cryptoWalletManagerGenerateEvent (cwm, (BRCryptoWalletManagerEvent) {
            CRYPTO_WALLET_MANAGER_EVENT_CHANGED,
            { .state = { oldState, newState }}
        });
}

extern BRCryptoAddressScheme
cryptoWalletManagerGetAddressScheme (BRCryptoWalletManager cwm) {
    pthread_mutex_lock (&cwm->lock);
    BRCryptoAddressScheme scheme = cwm->addressScheme;
    pthread_mutex_unlock (&cwm->lock);
    return scheme;
}

extern void
cryptoWalletManagerSetAddressScheme (BRCryptoWalletManager cwm,
                                     BRCryptoAddressScheme scheme) {
    pthread_mutex_lock (&cwm->lock);
    cwm->addressScheme = scheme;
    pthread_mutex_unlock (&cwm->lock);
}

extern const char *
cryptoWalletManagerGetPath (BRCryptoWalletManager cwm) {
    return cwm->path;
}

extern void
cryptoWalletManagerSetNetworkReachable (BRCryptoWalletManager cwm,
                                        BRCryptoBoolean isNetworkReachable) {
    if (NULL != cwm->p2pManager) {
        cryptoClientP2PManagerSetNetworkReachable (cwm->p2pManager, isNetworkReachable);
    }
}

//extern BRCryptoPeer
//cryptoWalletManagerGetPeer (BRCryptoWalletManager cwm) {
//    return (NULL == cwm->peer ? NULL : cryptoPeerTake (cwm->peer));
//}
//
//extern void
//cryptoWalletManagerSetPeer (BRCryptoWalletManager cwm,
//                            BRCryptoPeer peer) {
//    BRCryptoPeer oldPeer = cwm->peer;
//    cwm->peer = (NULL == peer ? NULL : cryptoPeerTake(peer));
//    if (NULL != oldPeer) cryptoPeerGive (oldPeer);
//}

extern BRCryptoWallet
cryptoWalletManagerCreateWallet (BRCryptoWalletManager cwm,
                                 BRCryptoCurrency currency) {
    BRCryptoWallet wallet = cryptoWalletManagerGetWalletForCurrency (cwm, currency);
    return (NULL == wallet
            ? cwm->handlers->createWallet (cwm, currency)
            : wallet);
}


extern BRCryptoWallet
cryptoWalletManagerGetWallet (BRCryptoWalletManager cwm) {
    return cryptoWalletTake (cwm->wallet);
}

extern BRCryptoWallet *
cryptoWalletManagerGetWallets (BRCryptoWalletManager cwm, size_t *count) {
    pthread_mutex_lock (&cwm->lock);
    *count = array_count (cwm->wallets);
    BRCryptoWallet *wallets = NULL;
    if (0 != *count) {
        wallets = calloc (*count, sizeof(BRCryptoWallet));
        for (size_t index = 0; index < *count; index++) {
            wallets[index] = cryptoWalletTake(cwm->wallets[index]);
        }
    }
    pthread_mutex_unlock (&cwm->lock);
    return wallets;
}


extern BRCryptoWallet
cryptoWalletManagerGetWalletForCurrency (BRCryptoWalletManager cwm,
                                         BRCryptoCurrency currency) {
    BRCryptoWallet wallet = NULL;
    pthread_mutex_lock (&cwm->lock);
    for (size_t index = 0; index < array_count(cwm->wallets); index++) {
        if (CRYPTO_TRUE == cryptoWalletHasCurrency (cwm->wallets[index], currency)) {
            wallet = cryptoWalletTake (cwm->wallets[index]);
            break;
        }
    }
    pthread_mutex_unlock (&cwm->lock);
    return wallet;
}

extern BRCryptoBoolean
cryptoWalletManagerHasWallet (BRCryptoWalletManager cwm,
                              BRCryptoWallet wallet) {
    BRCryptoBoolean r = CRYPTO_FALSE;
    pthread_mutex_lock (&cwm->lock);
    for (size_t index = 0; index < array_count (cwm->wallets) && CRYPTO_FALSE == r; index++) {
        r = cryptoWalletEqual(cwm->wallets[index], wallet);
    }
    pthread_mutex_unlock (&cwm->lock);
    return r;
}

extern void
cryptoWalletManagerAddWallet (BRCryptoWalletManager cwm,
                              BRCryptoWallet wallet) {
    pthread_mutex_lock (&cwm->lock);
    if (CRYPTO_FALSE == cryptoWalletManagerHasWallet (cwm, wallet)) {
        array_add (cwm->wallets, cryptoWalletTake (wallet));
        cryptoWalletManagerGenerateEvent (cwm, (BRCryptoWalletManagerEvent) {
            CRYPTO_WALLET_MANAGER_EVENT_WALLET_ADDED,
            { .wallet = cryptoWalletTake (wallet) }
        });
    }
    pthread_mutex_unlock (&cwm->lock);
}

extern void
cryptoWalletManagerRemWallet (BRCryptoWalletManager cwm,
                              BRCryptoWallet wallet) {

    BRCryptoWallet managerWallet = NULL;
    pthread_mutex_lock (&cwm->lock);
    for (size_t index = 0; index < array_count (cwm->wallets); index++) {
        if (CRYPTO_TRUE == cryptoWalletEqual(cwm->wallets[index], wallet)) {
            managerWallet = cwm->wallets[index];
            array_rm (cwm->wallets, index);
            cryptoWalletManagerGenerateEvent (cwm, (BRCryptoWalletManagerEvent) {
                CRYPTO_WALLET_MANAGER_EVENT_WALLET_DELETED,
                { .wallet = cryptoWalletTake (wallet) }
            });
            break;
        }
    }
    pthread_mutex_unlock (&cwm->lock);

    // drop reference outside of lock to avoid potential case where release function runs
    if (NULL != managerWallet) cryptoWalletGive (managerWallet);
}

// MARK: - Start/Stop

extern void
cryptoWalletManagerStart (BRCryptoWalletManager cwm) {
    // Start the CWM 'Event Handler'
    eventHandlerStart (cwm->handler);
//    eventHandlerStart (cwm->listenerHandler);

    // P2P Manager
    // QRY Manager

}

extern void
cryptoWalletManagerStop (BRCryptoWalletManager cwm) {
    // Stop the CWM 'Event Handler'
//    eventHandlerStop (cwm->listenerHandler);
    eventHandlerStop (cwm->handler);

    // P2P Manager
    // QRY Manager
}

/// MARK: - Connect/Disconnect/Sync

extern void
cryptoWalletManagerConnect (BRCryptoWalletManager cwm,
                            BRCryptoPeer peer) {
    switch (cwm->state.type) {
        case CRYPTO_WALLET_MANAGER_STATE_CREATED:
        case CRYPTO_WALLET_MANAGER_STATE_DISCONNECTED: {
            BRCryptoWalletManagerState oldState = cwm->state;
            BRCryptoWalletManagerState newState = cryptoWalletManagerStateInit (CRYPTO_WALLET_MANAGER_STATE_CONNECTED);

            cryptoClientQRYManagerConnect (cwm->qryManager);
            if (CRYPTO_CLIENT_P2P_MANAGER_TYPE == cwm->canSend.type ||
                CRYPTO_CLIENT_P2P_MANAGER_TYPE == cwm->canSync.type)
                cryptoClientP2PManagerConnect (cwm->p2pManager, peer);

            cryptoWalletManagerSetState (cwm, newState);

            cryptoWalletManagerGenerateEvent(cwm, (BRCryptoWalletManagerEvent) {
                CRYPTO_WALLET_MANAGER_EVENT_CHANGED,
                { .state = { oldState, newState }}
            });
            break;
        }
        case CRYPTO_WALLET_MANAGER_STATE_CONNECTED:
        case CRYPTO_WALLET_MANAGER_STATE_SYNCING:
            break;

        case CRYPTO_WALLET_MANAGER_STATE_DELETED:
            break;
    }
}

extern void
cryptoWalletManagerDisconnect (BRCryptoWalletManager cwm) {
    switch (cwm->state.type) {
        case CRYPTO_WALLET_MANAGER_STATE_CREATED:
        case CRYPTO_WALLET_MANAGER_STATE_CONNECTED:
        case CRYPTO_WALLET_MANAGER_STATE_SYNCING: {
            BRCryptoWalletManagerState oldState = cwm->state;
            BRCryptoWalletManagerState newState = cryptoWalletManagerStateDisconnectedInit (cryptoWalletManagerDisconnectReasonRequested());

            if (NULL != cwm->p2pManager) cryptoClientP2PManagerDisconnect (cwm->p2pManager);
            cryptoClientQRYManagerDisconnect (cwm->qryManager);

            cryptoWalletManagerSetState (cwm, newState);

            cryptoWalletManagerGenerateEvent(cwm, (BRCryptoWalletManagerEvent) {
                CRYPTO_WALLET_MANAGER_EVENT_CHANGED,
                { .state = { oldState, newState }}
            });
            break;
        }

        case CRYPTO_WALLET_MANAGER_STATE_DISCONNECTED:
            break;

        case CRYPTO_WALLET_MANAGER_STATE_DELETED:
            break;
    }
}

extern void
cryptoWalletManagerSync (BRCryptoWalletManager cwm) {
    cryptoWalletManagerSyncToDepth (cwm, CRYPTO_SYNC_DEPTH_FROM_CREATION);
}

extern void
cryptoWalletManagerSyncToDepth (BRCryptoWalletManager cwm,
                                BRCryptoSyncDepth depth) {
    if (CRYPTO_WALLET_MANAGER_STATE_CONNECTED == cwm->state.type)
        cryptoClientSync (cwm->canSync, depth, cryptoNetworkGetHeight(cwm->network));
}

// MARK: - Wipe


extern void
cryptoWalletManagerWipe (BRCryptoNetwork network,
                         const char *path) {

    const char *currencyName = cryptoBlockChainTypeGetCurrencyCode (cryptoNetworkGetType(network));
    const char *networkName  = cryptoNetworkGetDesc(network);

    fileServiceWipe (path, currencyName, networkName);
}

// MARK: - Create Transfer

private_extern void
cryptoWalletManagerSetTransferState (BRCryptoWalletManager cwm,
                                     BRCryptoWallet wallet,
                                     BRCryptoTransfer transfer,
                                     BRCryptoTransferState newState) {
    pthread_mutex_lock (&cwm->lock);

    BRCryptoTransferState oldState = cryptoTransferGetState (transfer);

    cryptoTransferSetState (transfer, newState);

    cryptoTransferStateRelease (&oldState);
    cryptoTransferStateRelease (&newState);

#ifdef REFACTOR
    //
    // If this is an error case, then we must remove the genericTransfer from the
    // genericWallet; otherwise the GEN balance and sequence number will be off.
    //
    // However, we leave the `transfer` in `wallet`.  And trouble is forecasted...
    //
    if (GENERIC_TRANSFER_STATE_ERRORED == newGenericState.type) {
        genWalletRemTransfer(cryptoWalletAsGEN(wallet), genericTransfer);

        BRCryptoAmount balance = cryptoWalletGetBalance(wallet);
        cwm->listener.walletEventCallback (cwm->listener.context,
                                           cryptoWalletManagerTake (cwm),
                                           cryptoWalletTake (cwm->wallet),
                                           (BRCryptoWalletEvent) {
                                               CRYPTO_WALLET_EVENT_BALANCE_UPDATED,
                                               { .balanceUpdated = { balance }}
                                           });

        cwm->listener.walletManagerEventCallback (cwm->listener.context,
                                                  cryptoWalletManagerTake (cwm),
                                                  (BRCryptoWalletManagerEvent) {
            CRYPTO_WALLET_MANAGER_EVENT_WALLET_CHANGED,
            { .wallet = cryptoWalletTake (cwm->wallet) }
        });
    }
#endif

    pthread_mutex_unlock (&cwm->lock);
}

extern BRCryptoTransfer
cryptoWalletManagerCreateTransfer (BRCryptoWalletManager cwm,
                                   BRCryptoWallet wallet,
                                   BRCryptoAddress target,
                                   BRCryptoAmount amount,
                                   BRCryptoFeeBasis estimatedFeeBasis,
                                   size_t attributesCount,
                                   OwnershipKept BRCryptoTransferAttribute *attributes) {
    return cryptoWalletCreateTransfer (wallet, target, amount,
                                       estimatedFeeBasis,
                                       attributesCount,
                                       attributes);
}

extern BRCryptoTransfer
cryptoWalletManagerCreateTransferMultiple (BRCryptoWalletManager cwm,
                                           BRCryptoWallet wallet,
                                           size_t outputsCount,
                                           BRCryptoTransferOutput *outputs,
                                           BRCryptoFeeBasis estimatedFeeBasis) {
    return cryptoWalletCreateTransferMultiple (wallet, outputsCount, outputs, estimatedFeeBasis);
}

// MARK: - Sign/Submit

extern BRCryptoBoolean
cryptoWalletManagerSign (BRCryptoWalletManager manager,
                         BRCryptoWallet wallet,
                         BRCryptoTransfer transfer,
                         const char *paperKey) {
    // Derived the seed used for signing.
    UInt512 seed = cryptoAccountDeriveSeed(paperKey);

    BRCryptoBoolean success = manager->handlers->signTransactionWithSeed (manager,
                                                                          wallet,
                                                                          transfer,
                                                                          seed);
    if (CRYPTO_TRUE == success)
        cryptoWalletManagerSetTransferState (manager, wallet, transfer,
                                             cryptoTransferStateInit (CRYPTO_TRANSFER_STATE_SIGNED));

    // Zero-out the seed.
    seed = UINT512_ZERO;

    return success;
}

static BRCryptoBoolean
cryptoWalletManagerSignWithKey (BRCryptoWalletManager manager,
                                BRCryptoWallet wallet,
                                BRCryptoTransfer transfer,
                                BRCryptoKey key) {
    BRCryptoBoolean success =  manager->handlers->signTransactionWithKey (manager,
                                                                          wallet,
                                                                          transfer,
                                                                          key);
    if (CRYPTO_TRUE == success)
        cryptoWalletManagerSetTransferState (manager, wallet, transfer,
                                             cryptoTransferStateInit (CRYPTO_TRANSFER_STATE_SIGNED));

    return success;
}

extern void
cryptoWalletManagerSubmitSigned (BRCryptoWalletManager cwm,
                                 BRCryptoWallet wallet,
                                 BRCryptoTransfer transfer) {

    cryptoWalletAddTransfer (wallet, transfer);

    cryptoClientSend (cwm->canSend, wallet, transfer);

    cryptoWalletGenerateEvent (wallet, (BRCryptoWalletEvent) {
        CRYPTO_WALLET_EVENT_TRANSFER_SUBMITTED,
        { .transfer = cryptoTransferTake (transfer) }
    });
}

extern void
cryptoWalletManagerSubmit (BRCryptoWalletManager manager,
                           BRCryptoWallet wallet,
                           BRCryptoTransfer transfer,
                           const char *paperKey) {

    if (CRYPTO_TRUE == cryptoWalletManagerSign (manager, wallet, transfer, paperKey))
        cryptoWalletManagerSubmitSigned (manager, wallet, transfer);
}

extern void
cryptoWalletManagerSubmitForKey (BRCryptoWalletManager manager,
                                 BRCryptoWallet wallet,
                                 BRCryptoTransfer transfer,
                                 BRCryptoKey key) {
    // Signing requires `key` to have a secret (that is, be a private key).
    if (!cryptoKeyHasSecret(key)) return;

    if (CRYPTO_TRUE == cryptoWalletManagerSignWithKey(manager, wallet, transfer, key))
        cryptoWalletManagerSubmitSigned (manager, wallet, transfer);
}

// MARK: - Estimate Limit/Fee

extern BRCryptoAmount
cryptoWalletManagerEstimateLimit (BRCryptoWalletManager manager,
                                  BRCryptoWallet  wallet,
                                  BRCryptoBoolean asMaximum,
                                  BRCryptoAddress target,
                                  BRCryptoNetworkFee fee,
                                  BRCryptoBoolean *needEstimate,
                                  BRCryptoBoolean *isZeroIfInsuffientFunds) {
    assert (NULL != needEstimate && NULL != isZeroIfInsuffientFunds);

    BRCryptoUnit unit = cryptoUnitGetBaseUnit (wallet->unit);

    // By default, we don't need an estimate
    *needEstimate = CRYPTO_FALSE;

    // By default, zero does not indicate insufficient funds
    *isZeroIfInsuffientFunds = CRYPTO_FALSE;

    BRCryptoAmount limit = manager->handlers->estimateLimit (manager,
                                                             wallet,
                                                             asMaximum,
                                                             target,
                                                             fee,
                                                             needEstimate,
                                                             isZeroIfInsuffientFunds,
                                                             unit);

    cryptoUnitGive (unit);
    return limit;
}


extern void
cryptoWalletManagerEstimateFeeBasis (BRCryptoWalletManager manager,
                                     BRCryptoWallet  wallet,
                                     BRCryptoCookie cookie,
                                     BRCryptoAddress target,
                                     BRCryptoAmount  amount,
                                     BRCryptoNetworkFee fee,
                                     size_t attributesCount,
                                     OwnershipKept BRCryptoTransferAttribute *attributes) {
    BRCryptoFeeBasis feeBasis = manager->handlers->estimateFeeBasis (manager,
                                                                     wallet,
                                                                     cookie,
                                                                     target,
                                                                     amount,
                                                                     fee,
                                                                     attributesCount,
                                                                     attributes);
    if (NULL != feeBasis)
        cryptoWalletGenerateEvent (wallet, (BRCryptoWalletEvent) {
            CRYPTO_WALLET_EVENT_FEE_BASIS_ESTIMATED,
            { .feeBasisEstimated = { CRYPTO_SUCCESS, cookie, feeBasis }} // feeBasis passed
        });
}

extern void
cryptoWalletManagerEstimateFeeBasisForPaymentProtocolRequest (BRCryptoWalletManager cwm,
                                                              BRCryptoWallet wallet,
                                                              BRCryptoCookie cookie,
                                                              BRCryptoPaymentProtocolRequest request,
                                                              BRCryptoNetworkFee fee) {
    const BRCryptoPaymentProtocolHandlers * paymentHandlers = cryptoHandlersLookup(cryptoWalletGetType(wallet))->payment;

    assert (NULL != paymentHandlers);

    BRCryptoFeeBasis feeBasis = paymentHandlers->estimateFeeBasis (request,
                                                                   cwm,
                                                                   wallet,
                                                                   cookie,
                                                                   fee);
    if (NULL != feeBasis)
        cryptoWalletGenerateEvent (wallet, (BRCryptoWalletEvent) {
            CRYPTO_WALLET_EVENT_FEE_BASIS_ESTIMATED,
            { .feeBasisEstimated = { CRYPTO_SUCCESS, cookie, feeBasis }} // feeBasis passed
        });
}

// MARK: - Sweeper

extern BRCryptoWalletSweeperStatus
cryptoWalletManagerWalletSweeperValidateSupported (BRCryptoWalletManager cwm,
                                                   BRCryptoWallet wallet,
                                                   BRCryptoKey key) {
    if (cryptoNetworkGetType (cwm->network) != cryptoWalletGetType (wallet)) {
        return CRYPTO_WALLET_SWEEPER_INVALID_ARGUMENTS;
    }
    
    if (CRYPTO_FALSE == cryptoKeyHasSecret (key)) {
        return CRYPTO_WALLET_SWEEPER_INVALID_KEY;
    }
    
    return cwm->handlers->validateSweeperSupported (cwm, wallet, key);
}

extern BRCryptoWalletSweeper
cryptoWalletManagerCreateWalletSweeper (BRCryptoWalletManager cwm,
                                        BRCryptoWallet wallet,
                                        BRCryptoKey key) {
    assert (cryptoKeyHasSecret (key));
    
    return cwm->handlers->createSweeper (cwm,
                                         wallet,
                                         key);
}

#ifdef REFACTOR
extern void
cryptoWalletManagerHandleTransferGENFilter (BRCryptoWalletManager cwm,
                                            OwnershipGiven BRGenericTransfer transferGeneric,
                                            BRCryptoBoolean needBalanceEvent) {
    int transferWasCreated = 0;

    // TODO: Determine the currency from `transferGeneric`
    BRCryptoCurrency currency   = cryptoNetworkGetCurrency   (cwm->network);
    BRCryptoUnit     unit       = cryptoNetworkGetUnitAsBase (cwm->network, currency);
    BRCryptoUnit     unitForFee = cryptoNetworkGetUnitAsBase (cwm->network, currency);
    BRCryptoWallet   wallet     = cryptoWalletManagerGetWalletForCurrency (cwm, currency);

    // TODO: I don't think any overall locks are needed here...

    // Look for a known transfer
    BRCryptoTransfer transfer = cryptoWalletFindTransferAsGEN (wallet, transferGeneric);

    // If we don't know about `transferGeneric`, create a crypto transfer
    if (NULL == transfer) {
        // Create the generic transfer... `transferGeneric` owned by `transfer`
        transfer = cryptoTransferCreateAsGEN (unit, unitForFee, transferGeneric);

        transferWasCreated = 1;
    }

    // We know 'transfer'; ensure it is up to date.  This is important for the case where
    // we created the transfer and then submitted it.  In that case `transfer` is what we
    // created and `transferGeneric` is what we recovered.  The recovered transfer will have
    // additional information - notably the UIDS.
    else {
        BRGenericTransfer transferGenericOrig = cryptoTransferAsGEN (transfer);

        // Update the UIDS
        if (NULL == genTransferGetUIDS(transferGenericOrig))
            genTransferSetUIDS (transferGenericOrig,
                                genTransferGetUIDS (transferGeneric));
    }

    // Fill in any attributes
    BRArrayOf(BRGenericTransferAttribute) genAttributes = genTransferGetAttributes(transferGeneric);
    BRArrayOf(BRCryptoTransferAttribute)  attributes;
    array_new(attributes, array_count(genAttributes));
    for (size_t index = 0; index < array_count(genAttributes); index++) {
        array_add (attributes,
                   cryptoTransferAttributeCreate (genTransferAttributeGetKey(genAttributes[index]),
                                                  genTransferAttributeGetVal(genAttributes[index]),
                                                  AS_CRYPTO_BOOLEAN (genTransferAttributeIsRequired(genAttributes[index]))));
    }
    cryptoTransferSetAttributes (transfer, attributes);
    array_free_all (attributes, cryptoTransferAttributeGive);

    // Set the state from `transferGeneric`.  This is where we move from 'submitted' to 'included'
    BRCryptoTransferState oldState = cryptoTransferGetState (transfer);
    BRCryptoTransferState newState = cryptoTransferStateCreateGEN (genTransferGetState(transferGeneric), unitForFee);
    cryptoTransferSetState (transfer, newState);

    if (!transferWasCreated)
        genTransferRelease(transferGeneric);

    // Save the transfer as it is now fully updated.
    genManagerSaveTransfer (cwm->u.gen, cryptoTransferAsGEN(transfer));

    // If we created the transfer...
    if (transferWasCreated) {
        // ... announce the newly created transfer.
        cwm->listener.transferEventCallback (cwm->listener.context,
                                             cryptoWalletManagerTake (cwm),
                                             cryptoWalletTake (wallet),
                                             cryptoTransferTake(transfer),
                                             (BRCryptoTransferEvent) {
            CRYPTO_TRANSFER_EVENT_CREATED
        });

        // ... cache the 'current' balance
        BRCryptoAmount oldBalance = cryptoWalletGetBalance (wallet);

        // ... add the transfer to its wallet...
        cryptoWalletAddTransfer (wallet, transfer);

        // ... tell 'generic wallet' about it.
        genWalletAddTransfer (cryptoWalletAsGEN(wallet), cryptoTransferAsGEN(transfer));

        // ... and announce the wallet's newly added transfer
        cwm->listener.walletEventCallback (cwm->listener.context,
                                           cryptoWalletManagerTake (cwm),
                                           cryptoWalletTake (wallet),
                                           (BRCryptoWalletEvent) {
            CRYPTO_WALLET_EVENT_TRANSFER_ADDED,
            { .transfer = { cryptoTransferTake (transfer) }}
        });

        // Get the new balance...
        BRCryptoAmount newBalance = cryptoWalletGetBalance(wallet);

        // ... if it differs from the old balance, geneate an event.
        if (CRYPTO_TRUE == needBalanceEvent && CRYPTO_COMPARE_EQ != cryptoAmountCompare(oldBalance, newBalance))
            cwm->listener.walletEventCallback (cwm->listener.context,
                                               cryptoWalletManagerTake (cwm),
                                               cryptoWalletTake (cwm->wallet),
                                               (BRCryptoWalletEvent) {
                                                CRYPTO_WALLET_EVENT_BALANCE_UPDATED,
                                                { .balanceUpdated = { newBalance }}
                                            });
        else cryptoAmountGive(newBalance);
        cryptoAmountGive(oldBalance);

        // Tell the manager that that wallet changed (added transfer, perhaps balance changed)
        cwm->listener.walletManagerEventCallback (cwm->listener.context,
                                                  cryptoWalletManagerTake (cwm),
                                                  (BRCryptoWalletManagerEvent) {
            CRYPTO_WALLET_MANAGER_EVENT_WALLET_CHANGED,
            { .wallet = cryptoWalletTake (cwm->wallet) }
        });
    }

    // If the state is not created and changed, announce a transfer state change.
    if (CRYPTO_TRANSFER_STATE_CREATED != newState.type && oldState.type != newState.type) {
        cwm->listener.transferEventCallback (cwm->listener.context,
                                             cryptoWalletManagerTake (cwm),
                                             cryptoWalletTake (wallet),
                                             cryptoTransferTake(transfer),
                                             (BRCryptoTransferEvent) {
            CRYPTO_TRANSFER_EVENT_CHANGED,
            { .state = {
                cryptoTransferStateCopy (&oldState),
                cryptoTransferStateCopy (&newState) }}
        });
    }

    cryptoTransferStateRelease (&oldState);
    cryptoTransferStateRelease (&newState);
    cryptoUnitGive(unitForFee);
    cryptoUnitGive(unit);
    cryptoTransferGive(transfer);
    cryptoWalletGive (wallet);
    cryptoCurrencyGive(currency);
}

extern void
cryptoWalletManagerHandleTransferGEN (BRCryptoWalletManager cwm,
                                      OwnershipGiven BRGenericTransfer transferGeneric) {
    cryptoWalletManagerHandleTransferGENFilter (cwm, transferGeneric, CRYPTO_TRUE);
}

static void
cryptoWalletManagerSyncCallbackGEN (BRGenericManagerSyncContext context,
                                    BRGenericManager manager,
                                    uint64_t begBlockHeight,
                                    uint64_t endBlockHeight,
                                    uint64_t fullSyncIncrement) {
    BRCryptoWalletManager cwm = cryptoWalletManagerTakeWeak ((BRCryptoWalletManager) context);
    if (NULL == cwm) return;

    // If the sync block range is larger than fullSyncIncrement, then this is a full sync.
    // Otherwise this is an ongoing, periodic sync - which we do not report.  It is as if in
    // P2P mode, a new block is announced.
    int fullSync = (endBlockHeight - begBlockHeight > fullSyncIncrement);

    pthread_mutex_lock (&cwm->lock);

    // If an ongoing sync, we are simply CONNECTED.
    BRCryptoWalletManagerState oldState = cwm->state;
    BRCryptoWalletManagerState newState = cryptoWalletManagerStateInit (fullSync
                                                                        ? CRYPTO_WALLET_MANAGER_STATE_SYNCING
                                                                        : CRYPTO_WALLET_MANAGER_STATE_CONNECTED);

    pthread_mutex_unlock (&cwm->lock);

    // Callback a Wallet Manager Event, but only on state changes.  We won't announce incremental
    // progress (with a blockHeight and timestamp.
    if (newState.type != oldState.type) {

        if (fullSync) {
            // Update the CWM state before SYNC_STARTED.
            cryptoWalletManagerSetState (cwm, newState);

            // Generate a SYNC_STARTED...
            cryptoWalletManagerListenerInvokeCallback (cwm->listener, cwm, (BRCryptoWalletManagerEvent) {
                CRYPTO_WALLET_MANAGER_EVENT_SYNC_STARTED
            });

            // ... and then a SYNC_CONTINUES at %100
            //            cwm->listener.walletManagerEventCallback (cwm->listener.context,
            //                                                      cryptoWalletManagerTake (cwm),
            //                                                      (BRCryptoWalletManagerEvent) {
            //                CRYPTO_WALLET_MANAGER_EVENT_SYNC_CONTINUES,
            //                { .syncContinues = { NO_CRYPTO_TIMESTAMP, 0 }}
            //            });
        }
        else {
            // Generate a SYNC_CONTINUES at %100...
            //            cwm->listener.walletManagerEventCallback (cwm->listener.context,
            //                                                      cryptoWalletManagerTake (cwm),
            //                                                      (BRCryptoWalletManagerEvent) {
            //                CRYPTO_WALLET_MANAGER_EVENT_SYNC_CONTINUES,
            //                { .syncContinues = { NO_CRYPTO_TIMESTAMP, 100 }}
            //            });

            // ... and then a CRYPTO_WALLET_MANAGER_EVENT_SYNC_STOPPED
            cryptoWalletManagerListenerInvokeCallback (cwm->listener, cwm, (BRCryptoWalletManagerEvent) {
                CRYPTO_WALLET_MANAGER_EVENT_SYNC_STOPPED,
                { .syncStopped = { CRYPTO_SYNC_STOPPED_REASON_COMPLETE }}
            });

            // Update the CWM state after SYNC_STOPPED.
             cryptoWalletManagerSetState (cwm, newState);
        }
    }

    cryptoWalletManagerGive (cwm);
}
#endif

extern const char *
cryptoWalletManagerEventTypeString (BRCryptoWalletManagerEventType t) {
    switch (t) {
        case CRYPTO_WALLET_MANAGER_EVENT_CREATED:
        return "CRYPTO_WALLET_MANAGER_EVENT_CREATED";

        case CRYPTO_WALLET_MANAGER_EVENT_CHANGED:
        return "CRYPTO_WALLET_MANAGER_EVENT_CHANGED";

        case CRYPTO_WALLET_MANAGER_EVENT_DELETED:
        return "CRYPTO_WALLET_MANAGER_EVENT_DELETED";

        case CRYPTO_WALLET_MANAGER_EVENT_WALLET_ADDED:
        return "CRYPTO_WALLET_MANAGER_EVENT_WALLET_ADDED";

        case CRYPTO_WALLET_MANAGER_EVENT_WALLET_CHANGED:
        return "CRYPTO_WALLET_MANAGER_EVENT_WALLET_CHANGED";

        case CRYPTO_WALLET_MANAGER_EVENT_WALLET_DELETED:
        return "CRYPTO_WALLET_MANAGER_EVENT_WALLET_DELETED";

        case CRYPTO_WALLET_MANAGER_EVENT_SYNC_STARTED:
        return "CRYPTO_WALLET_MANAGER_EVENT_SYNC_STARTED";

        case CRYPTO_WALLET_MANAGER_EVENT_SYNC_CONTINUES:
        return "CRYPTO_WALLET_MANAGER_EVENT_SYNC_CONTINUES";

        case CRYPTO_WALLET_MANAGER_EVENT_SYNC_STOPPED:
        return "CRYPTO_WALLET_MANAGER_EVENT_SYNC_STOPPED";

        case CRYPTO_WALLET_MANAGER_EVENT_SYNC_RECOMMENDED:
        return "CRYPTO_WALLET_MANAGER_EVENT_SYNC_RECOMMENDED";

        case CRYPTO_WALLET_MANAGER_EVENT_BLOCK_HEIGHT_UPDATED:
        return "CRYPTO_WALLET_MANAGER_EVENT_BLOCK_HEIGHT_UPDATED";
    }
    return "<CRYPTO_WALLET_MANAGER_EVENT_TYPE_UNKNOWN>";
}

/// MARK: Wallet Migrator

#if 1 // def REFACTOR
struct BRCryptoWalletMigratorRecord {
    BRFileService fileService;
    const char *fileServiceTransactionType;
    const char *fileServiceBlockType;
    const char *fileServicePeerType;

    int theErrorHackHappened;
    BRFileServiceError theErrorHack;
};

static void theErrorHackReset (BRCryptoWalletMigrator migrator) {
    migrator->theErrorHackHappened = 0;
}

//static void
//cryptoWalletMigratorErrorHandler (BRFileServiceContext context,
//                                  BRFileService fs,
//                                  BRFileServiceError error) {
//    // TODO: Racy on 'cryptoWalletMigratorRelease'?
//    BRCryptoWalletMigrator migrator = (BRCryptoWalletMigrator) context;
//
//    migrator->theErrorHackHappened = 1;
//    migrator->theErrorHack = error;
//}

extern BRCryptoWalletMigrator
cryptoWalletMigratorCreate (BRCryptoNetwork network,
                            const char *storagePath) {
    BRCryptoWalletMigrator migrator = calloc (1, sizeof (struct BRCryptoWalletMigratorRecord));

#ifdef REFACTOR
    migrator->fileService = BRWalletManagerCreateFileService (cryptoNetworkAsBTC(network),
                                                              storagePath,
                                                              migrator,
                                                              cryptoWalletMigratorErrorHandler);
    if (NULL == migrator->fileService) {
        cryptoWalletMigratorRelease(migrator);
        return NULL;
    }

    BRWalletManagerExtractFileServiceTypes (migrator->fileService,
                                            &migrator->fileServiceTransactionType,
                                            &migrator->fileServiceBlockType,
                                            &migrator->fileServicePeerType);
#endif
    return migrator;
}

extern void
cryptoWalletMigratorRelease (BRCryptoWalletMigrator migrator) {
    if (NULL != migrator->fileService) fileServiceRelease(migrator->fileService);

    memset (migrator, 0, sizeof(*migrator));
    free (migrator);
}

extern BRCryptoWalletMigratorStatus
cryptoWalletMigratorHandleTransactionAsBTC (BRCryptoWalletMigrator migrator,
                                            const uint8_t *bytes,
                                            size_t bytesCount,
                                            uint32_t blockHeight,
                                            uint32_t timestamp) {
    BRTransaction *tx = BRTransactionParse(bytes, bytesCount);
    if (NULL == tx)
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_ERROR_TRANSACTION
        };

    tx->blockHeight = blockHeight;
    tx->timestamp   = timestamp;

    // Calls cryptoWalletMigratorErrorHandler on error.
    theErrorHackReset(migrator);
    fileServiceSave (migrator->fileService, migrator->fileServiceTransactionType, tx);
    BRTransactionFree(tx);

    if (migrator->theErrorHackHappened)
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_ERROR_TRANSACTION
        };
    else
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_SUCCESS
        };
}

extern BRCryptoWalletMigratorStatus
cryptoWalletMigratorHandleBlockAsBTC (BRCryptoWalletMigrator migrator,
                                      BRCryptoData32 hash,
                                      uint32_t height,
                                      uint32_t nonce,
                                      uint32_t target,
                                      uint32_t txCount,
                                      uint32_t version,
                                      uint32_t timestamp,
                                      uint8_t *flags,  size_t flagsLen,
                                      BRCryptoData32 *hashes, size_t hashesCount,
                                      BRCryptoData32 merkleRoot,
                                      BRCryptoData32 prevBlock) {
    BRMerkleBlock *block = BRMerkleBlockNew();

    memcpy (block->blockHash.u8, hash.data, sizeof (hash.data));
    block->height = height;
    block->nonce  = nonce;
    block->target = target;
    block->totalTx = txCount;
    block->version = version;
    if (0 != timestamp) block->timestamp = timestamp;

    BRMerkleBlockSetTxHashes (block, (UInt256*) hashes, hashesCount, flags, flagsLen);

    memcpy (block->merkleRoot.u8, merkleRoot.data, sizeof (merkleRoot.data));
    memcpy (block->prevBlock.u8,  prevBlock.data,  sizeof (prevBlock.data));

    // ...
    theErrorHackReset(migrator);
    fileServiceSave (migrator->fileService, migrator->fileServiceBlockType, block);
    BRMerkleBlockFree (block);

    if (migrator->theErrorHackHappened)
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_ERROR_BLOCK
        };
    else
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_SUCCESS
        };
}

extern BRCryptoWalletMigratorStatus
cryptoWalletMigratorHandleBlockBytesAsBTC (BRCryptoWalletMigrator migrator,
                                           const uint8_t *bytes,
                                           size_t bytesCount,
                                           uint32_t height) {
    BRMerkleBlock *block = BRMerkleBlockParse (bytes, bytesCount);
    if (NULL == block)
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_ERROR_BLOCK
        };

    block->height = height;

    // ...
    theErrorHackReset(migrator);
    fileServiceSave (migrator->fileService, migrator->fileServiceBlockType, block);
    BRMerkleBlockFree (block);

    if (migrator->theErrorHackHappened)
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_ERROR_BLOCK
        };
    else
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_SUCCESS
        };
}

extern BRCryptoWalletMigratorStatus
cryptoWalletMigratorHandlePeerAsBTC (BRCryptoWalletMigrator migrator,
                                     uint32_t address,
                                     uint16_t port,
                                     uint64_t services,
                                     uint32_t timestamp) {
    BRPeer peer;

    peer.address = (UInt128) { .u32 = { 0, 0, 0xffff, address }};
    peer.port = port;
    peer.services = services;
    peer.timestamp = timestamp;
    peer.flags = 0;

    theErrorHackReset(migrator);
    fileServiceSave (migrator->fileService, migrator->fileServicePeerType, &peer);

    if (migrator->theErrorHackHappened)
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_ERROR_PEER
        };
    else
        return (BRCryptoWalletMigratorStatus) {
            CRYPTO_WALLET_MIGRATOR_SUCCESS
        };
}
#endif
/// MARK: Disconnect Reason

extern BRCryptoWalletManagerDisconnectReason
cryptoWalletManagerDisconnectReasonRequested(void) {
    return (BRCryptoWalletManagerDisconnectReason) {
        CRYPTO_WALLET_MANAGER_DISCONNECT_REASON_REQUESTED
    };
}

extern BRCryptoWalletManagerDisconnectReason
cryptoWalletManagerDisconnectReasonUnknown(void) {
    return (BRCryptoWalletManagerDisconnectReason) {
        CRYPTO_WALLET_MANAGER_DISCONNECT_REASON_UNKNOWN
    };
}

extern BRCryptoWalletManagerDisconnectReason
cryptoWalletManagerDisconnectReasonPosix(int errnum) {
    return (BRCryptoWalletManagerDisconnectReason) {
        CRYPTO_WALLET_MANAGER_DISCONNECT_REASON_POSIX,
        { .posix = { errnum } }
    };
}

extern char *
cryptoWalletManagerDisconnectReasonGetMessage(BRCryptoWalletManagerDisconnectReason *reason) {
    char *message = NULL;

    switch (reason->type) {
        case CRYPTO_WALLET_MANAGER_DISCONNECT_REASON_POSIX: {
            if (NULL != (message = strerror (reason->u.posix.errnum))) {
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

/// MARK: Sync Stopped Reason

extern BRCryptoSyncStoppedReason
cryptoSyncStoppedReasonComplete(void) {
    return (BRCryptoSyncStoppedReason) {
        CRYPTO_SYNC_STOPPED_REASON_COMPLETE
    };
}

extern BRCryptoSyncStoppedReason
cryptoSyncStoppedReasonRequested(void) {
    return (BRCryptoSyncStoppedReason) {
        CRYPTO_SYNC_STOPPED_REASON_REQUESTED
    };
}

extern BRCryptoSyncStoppedReason
cryptoSyncStoppedReasonUnknown(void) {
    return (BRCryptoSyncStoppedReason) {
        CRYPTO_SYNC_STOPPED_REASON_UNKNOWN
    };
}

extern BRCryptoSyncStoppedReason
cryptoSyncStoppedReasonPosix(int errnum) {
    return (BRCryptoSyncStoppedReason) {
        CRYPTO_SYNC_STOPPED_REASON_POSIX,
        { .posix = { errnum } }
    };
}

extern char *
cryptoSyncStoppedReasonGetMessage(BRCryptoSyncStoppedReason *reason) {
    char *message = NULL;

    switch (reason->type) {
        case CRYPTO_SYNC_STOPPED_REASON_POSIX: {
            if (NULL != (message = strerror (reason->u.posix.errnum))) {
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

/// MARK: Sync Mode

extern const char *
cryptoSyncModeString (BRCryptoSyncMode m) {
    switch (m) {
        case CRYPTO_SYNC_MODE_API_ONLY:
        return "CRYPTO_SYNC_MODE_API_ONLY";
        case CRYPTO_SYNC_MODE_API_WITH_P2P_SEND:
        return "CRYPTO_SYNC_MODE_API_WITH_P2P_SEND";
        case CRYPTO_SYNC_MODE_P2P_WITH_API_SYNC:
        return "CRYPTO_SYNC_MODE_P2P_WITH_API_SYNC";
        case CRYPTO_SYNC_MODE_P2P_ONLY:
        return "CRYPTO_SYNC_MODE_P2P_ONLY";
    }
}

// MARK: - Periodic Dispatcher

static void
cryptoWalletManagerPeriodicDispatcher (BREventHandler handler,
                                       BREventTimeout *event) {
    BRCryptoWalletManager cwm = (BRCryptoWalletManager) event->context;
    cryptoClientSyncPeriodic (cwm->canSync);
}

// MARK: - Transaction/Transfer Bundle

private_extern void
cryptoWalletManagerRecoverTransfersFromTransactionBundle (BRCryptoWalletManager cwm,
                                                          OwnershipKept BRCryptoClientTransactionBundle bundle) {
    cwm->handlers->recoverTransfersFromTransactionBundle (cwm, bundle);
}

private_extern void
cryptoWalletManagerRecoverTransferFromTransferBundle (BRCryptoWalletManager cwm,
                                                      OwnershipKept BRCryptoClientTransferBundle bundle) {
    cwm->handlers->recoverTransferFromTransferBundle (cwm, bundle);
}

private_extern void
cryptoWalletManagerRecoverTransferAttributesFromTransferBundle (BRCryptoWallet wallet,
                                                                BRCryptoTransfer transfer,
                                                                OwnershipKept BRCryptoClientTransferBundle bundle) {
    // If we are passed in attribues, they will replace any attribute already held
    // in `genTransfer`.  Specifically, for example, if we created an XRP transfer, then
    // we might have a 'DestinationTag'.  If the attributes provided do not include
    // 'DestinatinTag' then that attribute will be lost.  Losing such an attribute would
    // indicate a BlockSet error in processing transfers.
    if (bundle->attributesCount > 0) {
        BRCryptoAddress target = cryptoTransferGetTargetAddress (transfer);

        // Build the transfer attributes
        BRArrayOf(BRCryptoTransferAttribute) attributes;
        array_new(attributes, bundle->attributesCount);
        for (size_t index = 0; index < bundle->attributesCount; index++) {
            const char *key = bundle->attributeKeys[index];
            BRCryptoBoolean isRequiredAttribute;
            BRCryptoBoolean isAttribute = cryptoWalletHasTransferAttributeForKey (wallet,
                                                                                  target,
                                                                                  key,
                                                                                  &isRequiredAttribute);
            if (CRYPTO_TRUE == isAttribute)
                array_add (attributes,
                           cryptoTransferAttributeCreate(key,
                                                         bundle->attributeVals[index],
                                                         isRequiredAttribute));
        }
        
        cryptoTransferSetAttributes (transfer, array_count(attributes), attributes);
        cryptoTransferAttributeArrayRelease (attributes);
        cryptoAddressGive (target);
    }
}

private_extern BRCryptoFeeBasis
cryptoWalletManagerRecoverFeeBasisFromFeeEstimate (BRCryptoWalletManager cwm,
                                                   BRCryptoNetworkFee networkFee,
                                                   BRCryptoFeeBasis initialFeeBasis,
                                                   double costUnits,
                                                   size_t attributesCount,
                                                   OwnershipKept const char **attributeKeys,
                                                   OwnershipKept const char **attributeVals) {
    assert (NULL != cwm->handlers->recoverFeeBasisFromFeeEstimate); // not supported by chain
    return cwm->handlers->recoverFeeBasisFromFeeEstimate (cwm,
                                                          networkFee,
                                                          initialFeeBasis,
                                                          costUnits,
                                                          attributesCount,
                                                          attributeKeys,
                                                          attributeVals);
}
