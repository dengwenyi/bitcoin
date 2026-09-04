// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/chainstate_facade.h>

#include <chain.h>
#include <primitives/block.h>
#include <validation.h>

#include <utility>

namespace node {
namespace {

class ChainstateFacadeImpl final : public ChainstateFacade
{
public:
    explicit ChainstateFacadeImpl(ChainstateManager& chainman) : m_chainman{chainman} {}

    HeaderValidationEvent ProcessNewBlockHeaders(std::span<const CBlockHeader> headers,
                                                 bool min_pow_checked) override
    {
        BlockValidationState state;
        const CBlockIndex* block_index{nullptr};
        const bool accepted{m_chainman.ProcessNewBlockHeaders(headers, min_pow_checked, state, &block_index)};
        return {
            .accepted = accepted,
            .invalid = state.IsInvalid(),
            .result = state.GetResult(),
            .debug_message = state.GetDebugMessage(),
            .block_index = block_index,
        };
    }

    BlockProcessingEvent ProcessNewBlock(const std::shared_ptr<const CBlock>& block,
                                         bool force_processing,
                                         bool min_pow_checked) override
    {
        bool new_block{false};
        const bool accepted{m_chainman.ProcessNewBlock(block, force_processing, min_pow_checked, &new_block)};
        return {.accepted = accepted, .new_block = new_block};
    }

    CBlockLocator GetLocator(const CBlockIndex* block_index) const override
    {
        return ::GetLocator(block_index);
    }

    bool IsInitialBlockDownload() const override
    {
        return m_chainman.IsInitialBlockDownload();
    }

    const CBlockIndex* LookupBlockIndex(const uint256& hash) const override
    {
        return m_chainman.m_blockman.LookupBlockIndex(hash);
    }

    bool IsLoadingBlocks() const override
    {
        return m_chainman.m_blockman.LoadingBlocks();
    }

    bool IsPruneMode() const override
    {
        return m_chainman.m_blockman.IsPruneMode();
    }

    bool IsBlockPruned(const CBlockIndex& block_index) const override
    {
        return WITH_LOCK(m_chainman.GetMutex(), return m_chainman.m_blockman.IsBlockPruned(block_index));
    }

    std::optional<std::vector<std::byte>> ReadRawBlock(const FlatFilePos& position) const override
    {
        auto block{m_chainman.m_blockman.ReadRawBlock(position)};
        if (!block) return std::nullopt;
        return std::move(*block);
    }

    bool ReadBlock(CBlock& block, const FlatFilePos& position, const uint256& expected_hash) const override
    {
        return m_chainman.m_blockman.ReadBlock(block, position, expected_hash);
    }

    bool ReadBlock(CBlock& block, const CBlockIndex& block_index) const override
    {
        return m_chainman.m_blockman.ReadBlock(block, block_index);
    }

    const CBlockIndex* ActiveTip() const override
    {
        return m_chainman.ActiveTip();
    }

    int ActiveHeight() const override
    {
        return m_chainman.ActiveChain().Height();
    }

    bool ActiveContains(const CBlockIndex& block_index) const override
    {
        return m_chainman.ActiveChain().Contains(block_index);
    }

    const CBlockIndex* ActiveNext(const CBlockIndex& block_index) const override
    {
        return m_chainman.ActiveChain().Next(block_index);
    }

    const CBlockIndex* ActiveAtHeight(int height) const override
    {
        return m_chainman.ActiveChain()[height];
    }

    arith_uint256 MinimumChainWork() const override
    {
        return m_chainman.MinimumChainWork();
    }

    const CBlockIndex* BestHeader() const override
    {
        return m_chainman.m_best_header;
    }

    const CBlockIndex* EnsureBestHeader() override
    {
        if (m_chainman.m_best_header == nullptr) {
            m_chainman.m_best_header = m_chainman.ActiveTip();
        }
        return m_chainman.m_best_header;
    }

    const CBlockIndex* UnvalidatedSnapshotBase() const override
    {
        const Chainstate& current{m_chainman.CurrentChainstate()};
        const CBlockIndex* snapshot_base{current.SnapshotBase()};
        return snapshot_base && current.m_assumeutxo == Assumeutxo::UNVALIDATED ? snapshot_base : nullptr;
    }

    bool IsSegwitActiveAt(const CBlockIndex& block_index) const override
    {
        return DeploymentActiveAt(block_index, m_chainman, Consensus::DEPLOYMENT_SEGWIT);
    }

    bool IsSegwitActiveAfter(const CBlockIndex* previous_block) const override
    {
        return DeploymentActiveAfter(previous_block, m_chainman, Consensus::DEPLOYMENT_SEGWIT);
    }

    ChainActivationEvent ActivateBestChain(const std::shared_ptr<const CBlock>& recent_block) override
    {
        BlockValidationState state;
        const bool accepted{m_chainman.ActiveChainstate().ActivateBestChain(state, recent_block)};
        return {.accepted = accepted, .description = state.ToString()};
    }

    const CBlockIndex* FindForkInGlobalIndex(const CBlockLocator& locator) const override
    {
        return m_chainman.ActiveChainstate().FindForkInGlobalIndex(locator);
    }

    void ReportHeadersPresync(int64_t height, int64_t timestamp) override
    {
        m_chainman.ReportHeadersPresync(height, timestamp);
    }

    std::optional<std::pair<const CBlockIndex*, const CBlockIndex*>> GetHistoricalBlockRange() const override
    {
        return m_chainman.GetHistoricalBlockRange();
    }

private:
    ChainstateManager& m_chainman;
};

} // namespace

std::unique_ptr<ChainstateFacade> MakeChainstateFacade(ChainstateManager& chainman)
{
    return std::make_unique<ChainstateFacadeImpl>(chainman);
}

} // namespace node
