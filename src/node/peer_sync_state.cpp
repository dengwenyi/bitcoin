// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/peer_sync_state.h>

#include <blockencodings.h>

#include <utility>

namespace node {

QueuedBlock::QueuedBlock(const CBlockIndex* block_index, std::unique_ptr<PartiallyDownloadedBlock> partial_block)
    : pindex{block_index}, partialBlock{std::move(partial_block)}
{
}

QueuedBlock::QueuedBlock(QueuedBlock&&) noexcept = default;
QueuedBlock& QueuedBlock::operator=(QueuedBlock&&) noexcept = default;
QueuedBlock::~QueuedBlock() = default;
PeerSyncState::~PeerSyncState() = default;

} // namespace node
