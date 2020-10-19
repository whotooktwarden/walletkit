//
//  BRCryptoFileService.c
//  Core
//
//  Created by Ehsan Rezaie on 2020-05-14.
//  Copyright © 2019 Breadwallet AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//

#include "BRCryptoFileService.h"
#include "BRCryptoHashP.h"
#include "BRCryptoTransferP.h"
#include "BRCryptoWalletManagerP.h"

private_extern UInt256
fileServiceTypeTransferV1Identifier (BRFileServiceContext context,
                                     BRFileService fs,
                                     const void *entity) {
    BRCryptoWalletManager manager = (BRCryptoWalletManager) context; (void) manager;
    BRCryptoTransfer     transfer = (BRCryptoTransfer) entity;

    BRCryptoHash transferHash = cryptoTransferGetHash(transfer);

    size_t bytesCount;
    const uint8_t *bytes = cryptoHashGetBytes (transferHash, &bytesCount);
    if (bytesCount > sizeof (UInt256)) bytesCount = sizeof (UInt256);

    UInt256 identifier = UINT256_ZERO;
    memcpy (identifier.u8, bytes, bytesCount);

    cryptoHashGive (transferHash);

    return identifier;
}

private_extern void *
fileServiceTypeTransferV1Reader (BRFileServiceContext context,
                                 BRFileService fs,
                                 uint8_t *bytes,
                                 uint32_t bytesCount) {
    BRCryptoWalletManager manager = (BRCryptoWalletManager) context;

    BRRlpCoder coder = rlpCoderCreate();
    BRRlpData  data  = (BRRlpData) { bytesCount, bytes };
    BRRlpItem  item  = rlpDataGetItem (coder, data);

    BRCryptoTransfer transfer = cryptoTransferRLPDecode (item, manager->network, coder);

    rlpItemRelease (coder, item);
    rlpCoderRelease(coder);

    return transfer;
}

private_extern uint8_t *
fileServiceTypeTransferV1Writer (BRFileServiceContext context,
                                 BRFileService fs,
                                 const void* entity,
                                 uint32_t *bytesCount) {
    BRCryptoWalletManager manager    = (BRCryptoWalletManager) context;
    BRCryptoTransfer     transfer    = (BRCryptoTransfer) entity;

    BRRlpCoder coder = rlpCoderCreate();
    BRRlpItem item   = cryptoTransferRLPEncode (transfer, manager->network, coder);
    BRRlpData data   = rlpItemGetData (coder, item);

    rlpItemRelease (coder, item);
    rlpCoderRelease (coder);

    *bytesCount = (uint32_t) data.bytesCount;
    return data.bytes;
}

private_extern BRArrayOf(BRCryptoTransfer)
initialTransfersLoad (BRCryptoWalletManager manager) {
    BRSetOf(BRCryptoTransfer*) transferSet = cryptoTransferSetCreate(100);
    if (1 != fileServiceLoad (manager->fileService, transferSet, fileServiceTypeTransfers, 1)) {
        cryptoTransferSetRelease(transferSet);
        printf ("CRY: failed to load transactions");
        return NULL;
    }

    size_t transfersCount = BRSetCount(transferSet);

    BRArrayOf(BRCryptoTransfer) transfers;
    array_new (transfers, transfersCount);
    array_set_count(transfers, transfersCount);

    BRSetAll(transferSet, (void**) transfers, transfersCount);
    BRSetFree(transferSet); // Don't release => don't give transfers in `transfers`

    printf ("CRY: loaded %zu transfers\n", transfersCount);
    return transfers;
}
