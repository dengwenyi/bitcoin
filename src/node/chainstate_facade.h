// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_CHAINSTATE_FACADE_H
#define BITCOIN_NODE_CHAINSTATE_FACADE_H

#include <arith_uint256.h>
#include <consensus/validation.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

class CBlock;
class CBlockHeader;
class CBlockIndex;
class ChainstateManager;
class uint256;
struct CBlockLocator;
struct FlatFilePos;

namespace node {

/** Typed result of validating a header sequence at the chainstate boundary. */
struct HeaderValidationEvent {
    bool accepted{false};
    bool invalid{false};
    BlockValidationResult result{BlockValidationResult::BLOCK_RESULT_UNSET};
    std::string debug_message;
    const CBlockIndex* block_index{nullptr};
};

/** Typed result of attempting to activate the best known chain. */
struct ChainActivationEvent {
    bool accepted{false};
    std::string description;
};

/** Typed result of submitting a block to validation. */
struct BlockProcessingEvent {
    bool accepted{false};
    bool new_block{false};
};

/** Narrow validation port used by peer processing for chain mutations and sync state. */
class ChainstateFacade
{
public:
    virtual ~ChainstateFacade() = default;

    virtual HeaderValidationEvent ProcessNewBlockHeaders(std::span<const CBlockHeader> headers,
                                                         bool min_pow_checked) = 0;
    virtual BlockProcessingEvent ProcessNewBlock(const std::shared_ptr<const CBlock>& block,
                                                 bool force_processing,
                                                 bool min_pow_checked) = 0;
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

    /** Read-only chain view. Callers preserve the existing cs_main locking contract. */
    virtual const CBlockIndex* ActiveTip() const = 0;
    virtual int ActiveHeight() const = 0;
    virtual bool ActiveContains(const CBlockIndex& block_index) const = 0;
    virtual const CBlockIndex* ActiveNext(const CBlockIndex& block_index) const = 0;
    virtual const CBlockIndex* ActiveAtHeight(int height) const = 0;
    virtual arith_uint256 MinimumChainWork() const = 0;
    virtual const CBlockIndex* BestHeader() const = 0;
    virtual const CBlockIndex* EnsureBestHeader() = 0;

    /** Return the active snapshot base only while its background validation is incomplete. */
    virtual const CBlockIndex* UnvalidatedSnapshotBase() const = 0;
    virtual bool IsSegwitActiveAt(const CBlockIndex& block_index) const = 0;
    virtual bool IsSegwitActiveAfter(const CBlockIndex* previous_block) const = 0;
    virtual ChainActivationEvent ActivateBestChain(const std::shared_ptr<const CBlock>& recent_block) = 0;
    virtual const CBlockIndex* FindForkInGlobalIndex(const CBlockLocator& locator) const = 0;
    virtual void ReportHeadersPresync(int64_t height, int64_t timestamp) = 0;
    virtual std::optional<std::pair<const CBlockIndex*, const CBlockIndex*>> GetHistoricalBlockRange() const = 0;
};

std::unique_ptr<ChainstateFacade> MakeChainstateFacade(ChainstateManager& chainman);

} // namespace node

#endif // BITCOIN_NODE_CHAINSTATE_FACADE_H
