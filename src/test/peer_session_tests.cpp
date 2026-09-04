// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/peer_session.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(peer_session_tests)

BOOST_AUTO_TEST_CASE(immutable_identity)
{
    constexpr NodeId id{42};
    constexpr ServiceFlags services{NODE_NETWORK | NODE_WITNESS};
    node::PeerSession outbound{id, services, /*is_inbound=*/false};

    BOOST_CHECK_EQUAL(outbound.m_id, id);
    BOOST_CHECK(outbound.m_our_services == services);
    BOOST_CHECK(!outbound.m_is_inbound);
    BOOST_CHECK(outbound.m_their_services == NODE_NONE);
    BOOST_CHECK_EQUAL(outbound.m_ping_nonce_sent.load(), 0U);
    BOOST_CHECK(outbound.m_ping_start.load() == NodeClock::epoch);
    BOOST_CHECK(!outbound.m_ping_queued.load());
    BOOST_CHECK(!outbound.m_wtxid_relay.load());
    BOOST_CHECK(!outbound.m_addr_relay_enabled.load());
    BOOST_CHECK(!outbound.m_wants_addrv2.load());
    BOOST_CHECK(!outbound.m_sent_sendheaders.load());
    BOOST_CHECK_EQUAL(outbound.m_addr_rate_limited.load(), 0U);
    BOOST_CHECK_EQUAL(outbound.m_addr_processed.load(), 0U);
    BOOST_CHECK(outbound.m_time_offset.load() == std::chrono::seconds{0});

    outbound.m_their_services = NODE_NETWORK_LIMITED;
    outbound.m_ping_queued = true;
    outbound.m_wtxid_relay = true;
    outbound.m_time_offset = std::chrono::seconds{7};
    BOOST_CHECK(outbound.m_their_services.load() == NODE_NETWORK_LIMITED);
    BOOST_CHECK(outbound.m_ping_queued.load());
    BOOST_CHECK(outbound.m_wtxid_relay.load());
    BOOST_CHECK(outbound.m_time_offset.load() == std::chrono::seconds{7});

    BOOST_CHECK(!outbound.ConsumeShouldDiscourage());
    outbound.MarkForDiscouragement();
    BOOST_CHECK(outbound.ConsumeShouldDiscourage());
    BOOST_CHECK(!outbound.ConsumeShouldDiscourage());

    outbound.WithBlockAnnouncements([](auto& announcements) {
        BOOST_CHECK(announcements.m_blocks_for_inv_relay.empty());
        BOOST_CHECK(announcements.m_blocks_for_headers_relay.empty());
        BOOST_CHECK(announcements.m_continuation_block.IsNull());
        announcements.m_blocks_for_inv_relay.push_back(uint256::ONE);
        announcements.m_blocks_for_headers_relay.push_back(uint256::ONE);
        announcements.m_continuation_block = uint256::ONE;
    });
    outbound.WithBlockAnnouncements([](auto& announcements) {
        BOOST_CHECK_EQUAL(announcements.m_blocks_for_inv_relay.size(), 1U);
        BOOST_CHECK_EQUAL(announcements.m_blocks_for_headers_relay.size(), 1U);
        BOOST_CHECK(announcements.m_continuation_block == uint256::ONE);
    });

    const node::PeerSession inbound{id + 1, NODE_NONE, /*is_inbound=*/true};
    BOOST_CHECK_EQUAL(inbound.m_id, id + 1);
    BOOST_CHECK(inbound.m_our_services == NODE_NONE);
    BOOST_CHECK(inbound.m_is_inbound);
}

BOOST_AUTO_TEST_SUITE_END()
