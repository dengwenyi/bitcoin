// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_PEER_SYNC_STATE_H
#define BITCOIN_NODE_PEER_SYNC_STATE_H

#include <uint256.h>

class CBlockIndex;

namespace node {

/** Chain-view state maintained independently for each peer. */
struct PeerSyncState {
    /** Best known block announced by the peer. */
    const CBlockIndex* pindexBestKnownBlock{nullptr};
    /** Last announced block whose index was not yet known. */
    uint256 hashLastUnknownBlock;
    /** Last full block shared by the local and peer chains. */
    const CBlockIndex* pindexLastCommonBlock{nullptr};
    /** Best header sent to the peer. */
    const CBlockIndex* pindexBestHeaderSent{nullptr};
};

} // namespace node

#endif // BITCOIN_NODE_PEER_SYNC_STATE_H
