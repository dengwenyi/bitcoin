// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_PEER_SYNC_STATE_H
#define BITCOIN_NODE_PEER_SYNC_STATE_H

#include <uint256.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>

class CBlockIndex;
class PartiallyDownloadedBlock;

namespace node {

/** One validated-header block currently scheduled for download. */
struct QueuedBlock {
    const CBlockIndex* pindex;
    std::unique_ptr<PartiallyDownloadedBlock> partialBlock;

    QueuedBlock(const CBlockIndex* block_index, std::unique_ptr<PartiallyDownloadedBlock> partial_block);
    QueuedBlock(QueuedBlock&&) noexcept;
    QueuedBlock& operator=(QueuedBlock&&) noexcept;
    QueuedBlock(const QueuedBlock&) = delete;
    QueuedBlock& operator=(const QueuedBlock&) = delete;
    ~QueuedBlock();
};

/** Chain-view state maintained independently for each peer. */
struct PeerSyncState {
    ~PeerSyncState();

    /** Best known block announced by the peer. */
    const CBlockIndex* pindexBestKnownBlock{nullptr};
    /** Last announced block whose index was not yet known. */
    uint256 hashLastUnknownBlock;
    /** Last full block shared by the local and peer chains. */
    const CBlockIndex* pindexLastCommonBlock{nullptr};
    /** Best header sent to the peer. */
    const CBlockIndex* pindexBestHeaderSent{nullptr};

    bool fSyncStarted{false};
    std::chrono::microseconds m_stalling_since{0};
    std::list<QueuedBlock> vBlocksInFlight;
    std::chrono::microseconds m_downloading_since{0};
    bool fPreferredDownload{false};
    bool m_requested_hb_cmpctblocks{false};
    bool m_provides_cmpctblocks{false};

    struct ChainSyncTimeoutState {
        std::chrono::seconds m_timeout{0};
        const CBlockIndex* m_work_header{nullptr};
        bool m_sent_getheaders{false};
        bool m_protect{false};
    };

    ChainSyncTimeoutState m_chain_sync;
    int64_t m_last_block_announcement{0};
};

} // namespace node

#endif // BITCOIN_NODE_PEER_SYNC_STATE_H
