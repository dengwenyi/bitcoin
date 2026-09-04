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
}

BOOST_AUTO_TEST_SUITE_END()
