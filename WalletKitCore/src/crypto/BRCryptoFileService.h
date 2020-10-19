//
//  BRCryptoFileService.h
//  Core
//
//  Created by Ehsan Rezaie on 2020-05-14.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#ifndef BRCryptoFileService_h
#define BRCryptoFileService_h

#include "support/BRArray.h"
#include "BRCryptoBase.h"
#include "ethereum/util/BRUtil.h"
#include "support/BRFileService.h"

#ifdef __cplusplus
extern "C" {
#endif

#define fileServiceTypeTransfers      "transfers"

typedef enum {
    CRYPTO_FILE_SERVICE_TRANSFER_BASE_VERSION_1,
} BRCryptoFileServiceTransferBaseVersion;

#define cryptoFileServiceTransferVersionCreate( base, type )    ((BRFileServiceVersion) (((base) << 4) | (type)))

private_extern UInt256
fileServiceTypeTransferV1Identifier (BRFileServiceContext context,
                                     BRFileService fs,
                                     const void *entity);

private_extern void *
fileServiceTypeTransferV1Reader (BRFileServiceContext context,
                                 BRFileService fs,
                                 uint8_t *bytes,
                                 uint32_t bytesCount);

private_extern uint8_t *
fileServiceTypeTransferV1Writer (BRFileServiceContext context,
                                 BRFileService fs,
                                 const void* entity,
                                 uint32_t *bytesCount);

private_extern BRArrayOf(BRCryptoTransfer)
initialTransfersLoad (BRCryptoWalletManager manager);

#endif /* BRCryptoFileService_h */
