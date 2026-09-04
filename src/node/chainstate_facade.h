// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_CHAINSTATE_FACADE_H
#define BITCOIN_NODE_CHAINSTATE_FACADE_H

#include <memory>
#include <span>

class BlockValidationState;
class CBlock;
class CBlockHeader;
class CBlockIndex;
class ChainstateManager;
struct CBlockLocator;

namespace node {

/** Narrow validation port used by peer processing for chain mutations and sync state. */
class ChainstateFacade
{
public:
    virtual ~ChainstateFacade() = default;

    virtual bool ProcessNewBlockHeaders(std::span<const CBlockHeader> headers,
                                        bool min_pow_checked,
                                        BlockValidationState& state,
                                        const CBlockIndex** block_index = nullptr) = 0;
    virtual bool ProcessNewBlock(const std::shared_ptr<const CBlock>& block,
                                 bool force_processing,
                                 bool min_pow_checked,
                                 bool* new_block) = 0;
    virtual CBlockLocator GetLocator(const CBlockIndex* block_index) const = 0;
    virtual bool IsInitialBlockDownload() const = 0;
};

std::unique_ptr<ChainstateFacade> MakeChainstateFacade(ChainstateManager& chainman);

} // namespace node

#endif // BITCOIN_NODE_CHAINSTATE_FACADE_H
