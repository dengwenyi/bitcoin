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
    const node::PeerSession outbound{id, services, /*is_inbound=*/false};

    BOOST_CHECK_EQUAL(outbound.m_id, id);
    BOOST_CHECK(outbound.m_our_services == services);
    BOOST_CHECK(!outbound.m_is_inbound);

    const node::PeerSession inbound{id + 1, NODE_NONE, /*is_inbound=*/true};
    BOOST_CHECK_EQUAL(inbound.m_id, id + 1);
    BOOST_CHECK(inbound.m_our_services == NODE_NONE);
    BOOST_CHECK(inbound.m_is_inbound);
}

BOOST_AUTO_TEST_SUITE_END()
