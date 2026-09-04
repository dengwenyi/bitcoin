// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/chainstate_facade.h>

#include <chain.h>
#include <primitives/block.h>
#include <validation.h>

namespace node {
namespace {

class ChainstateFacadeImpl final : public ChainstateFacade
{
public:
    explicit ChainstateFacadeImpl(ChainstateManager& chainman) : m_chainman{chainman} {}

    bool ProcessNewBlockHeaders(std::span<const CBlockHeader> headers,
                                bool min_pow_checked,
                                BlockValidationState& state,
                                const CBlockIndex** block_index) override
    {
        return m_chainman.ProcessNewBlockHeaders(headers, min_pow_checked, state, block_index);
    }

    bool ProcessNewBlock(const std::shared_ptr<const CBlock>& block,
                         bool force_processing,
                         bool min_pow_checked,
                         bool* new_block) override
    {
        return m_chainman.ProcessNewBlock(block, force_processing, min_pow_checked, new_block);
    }

    CBlockLocator GetLocator(const CBlockIndex* block_index) const override
    {
        return ::GetLocator(block_index);
    }

    bool IsInitialBlockDownload() const override
    {
        return m_chainman.IsInitialBlockDownload();
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
