// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/peer_sync_state.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(peer_sync_state_tests)

BOOST_AUTO_TEST_CASE(default_chain_view)
{
    node::PeerSyncState state;
    BOOST_CHECK_EQUAL(state.pindexBestKnownBlock, nullptr);
    BOOST_CHECK(state.hashLastUnknownBlock.IsNull());
    BOOST_CHECK_EQUAL(state.pindexLastCommonBlock, nullptr);
    BOOST_CHECK_EQUAL(state.pindexBestHeaderSent, nullptr);
    BOOST_CHECK(!state.fSyncStarted);
    BOOST_CHECK(state.m_stalling_since == std::chrono::microseconds{0});
    BOOST_CHECK(state.m_downloading_since == std::chrono::microseconds{0});
    BOOST_CHECK(!state.fPreferredDownload);
    BOOST_CHECK(!state.m_requested_hb_cmpctblocks);
    BOOST_CHECK(!state.m_provides_cmpctblocks);
    BOOST_CHECK(state.m_chain_sync.m_timeout == std::chrono::seconds{0});
    BOOST_CHECK_EQUAL(state.m_chain_sync.m_work_header, nullptr);
    BOOST_CHECK(!state.m_chain_sync.m_sent_getheaders);
    BOOST_CHECK(!state.m_chain_sync.m_protect);
    BOOST_CHECK_EQUAL(state.m_last_block_announcement, 0);
}

BOOST_AUTO_TEST_SUITE_END()
