// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <node/chainstate_facade.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chainstate_facade_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(query_forwarding)
{
    auto facade{node::MakeChainstateFacade(*m_node.chainman)};

    BOOST_CHECK_EQUAL(facade->IsInitialBlockDownload(), m_node.chainman->IsInitialBlockDownload());

    const CBlockIndex* tip{WITH_LOCK(cs_main, return m_node.chainman->ActiveTip())};
    BOOST_REQUIRE(tip != nullptr);
    BOOST_CHECK(facade->GetLocator(tip).vHave == ::GetLocator(tip).vHave);
    const CBlockIndex* looked_up{WITH_LOCK(cs_main, return facade->LookupBlockIndex(tip->GetBlockHash()))};
    BOOST_CHECK_EQUAL(looked_up, tip);
    BOOST_CHECK_EQUAL(facade->IsLoadingBlocks(), m_node.chainman->m_blockman.LoadingBlocks());
    BOOST_CHECK_EQUAL(facade->IsPruneMode(), m_node.chainman->m_blockman.IsPruneMode());

    {
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(facade->ActiveTip(), m_node.chainman->ActiveTip());
        BOOST_CHECK_EQUAL(facade->ActiveHeight(), m_node.chainman->ActiveChain().Height());
        BOOST_CHECK(facade->ActiveContains(*tip));
        BOOST_CHECK_EQUAL(facade->ActiveAtHeight(tip->nHeight), tip);
        BOOST_CHECK(facade->MinimumChainWork() == m_node.chainman->MinimumChainWork());
        BOOST_CHECK_EQUAL(facade->BestHeader(), m_node.chainman->m_best_header);
        BOOST_CHECK_EQUAL(facade->EnsureBestHeader(), m_node.chainman->m_best_header);
        BOOST_CHECK_EQUAL(facade->UnvalidatedSnapshotBase(), nullptr);
        BOOST_CHECK_EQUAL(facade->IsSegwitActiveAt(*tip), DeploymentActiveAt(*tip, *m_node.chainman, Consensus::DEPLOYMENT_SEGWIT));
        BOOST_CHECK_EQUAL(facade->IsSegwitActiveAfter(tip), DeploymentActiveAfter(tip, *m_node.chainman, Consensus::DEPLOYMENT_SEGWIT));
        BOOST_CHECK_EQUAL(facade->FindForkInGlobalIndex(facade->GetLocator(tip)), tip);
        BOOST_CHECK(facade->GetHistoricalBlockRange() == m_node.chainman->GetHistoricalBlockRange());
    }

    const auto activation{facade->ActivateBestChain({})};
    BOOST_CHECK(activation.accepted);

    const auto headers{facade->ProcessNewBlockHeaders({{Params().GenesisBlock()}}, /*min_pow_checked=*/true)};
    BOOST_CHECK(headers.accepted);
    BOOST_CHECK(!headers.invalid);
    BOOST_CHECK(headers.result == BlockValidationResult::BLOCK_RESULT_UNSET);
    BOOST_CHECK_EQUAL(headers.block_index, tip);

    CBlockHeader invalid_header{Params().GenesisBlock()};
    invalid_header.nBits = 0;
    const auto invalid_headers{facade->ProcessNewBlockHeaders(
        {{invalid_header}}, /*min_pow_checked=*/true)};
    BOOST_CHECK(!invalid_headers.accepted);
    BOOST_CHECK(invalid_headers.invalid);
    BOOST_CHECK(invalid_headers.result == BlockValidationResult::BLOCK_INVALID_HEADER);
    BOOST_CHECK(!invalid_headers.debug_message.empty());
    BOOST_CHECK_EQUAL(invalid_headers.block_index, nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
