// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_CHAINSTATE_FACADE_H
#define BITCOIN_NODE_CHAINSTATE_FACADE_H

#include <arith_uint256.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class BlockValidationState;
class CBlock;
class CBlockHeader;
class CBlockIndex;
class ChainstateManager;
class uint256;
struct CBlockLocator;
struct FlatFilePos;

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

    /** Look up an index entry. The caller must hold cs_main while using the returned pointer. */
    virtual const CBlockIndex* LookupBlockIndex(const uint256& hash) const = 0;
    virtual bool IsLoadingBlocks() const = 0;
    virtual bool IsPruneMode() const = 0;
    virtual bool IsBlockPruned(const CBlockIndex& block_index) const = 0;
    virtual std::optional<std::vector<std::byte>> ReadRawBlock(const FlatFilePos& position) const = 0;
    virtual bool ReadBlock(CBlock& block, const FlatFilePos& position, const uint256& expected_hash) const = 0;
    virtual bool ReadBlock(CBlock& block, const CBlockIndex& block_index) const = 0;

    virtual const CBlockIndex* ActiveTip() const = 0;
    virtual int ActiveHeight() const = 0;
    virtual bool ActiveContains(const CBlockIndex& block_index) const = 0;
    virtual const CBlockIndex* ActiveNext(const CBlockIndex& block_index) const = 0;
    virtual const CBlockIndex* ActiveAtHeight(int height) const = 0;
    virtual arith_uint256 MinimumChainWork() const = 0;
    virtual const CBlockIndex* BestHeader() const = 0;
    virtual const CBlockIndex* EnsureBestHeader() = 0;
};

std::unique_ptr<ChainstateFacade> MakeChainstateFacade(ChainstateManager& chainman);

} // namespace node

#endif // BITCOIN_NODE_CHAINSTATE_FACADE_H
