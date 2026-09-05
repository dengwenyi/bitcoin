// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/peer_tx_relay.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(peer_tx_relay_tests)

BOOST_AUTO_TEST_CASE(optional_state)
{
    node::PeerTxRelayState state;
    BOOST_CHECK(state.GetTxRelay() == nullptr);

    auto* relay{state.SetTxRelay()};
    BOOST_REQUIRE(relay != nullptr);
    BOOST_CHECK(state.GetTxRelay() == relay);
    BOOST_CHECK(!WITH_LOCK(relay->m_bloom_filter_mutex, return relay->m_relay_txs));
    BOOST_CHECK(WITH_LOCK(relay->m_tx_inventory_mutex, return relay->m_tx_inventory_to_send.empty()));
    BOOST_CHECK(!WITH_LOCK(relay->m_tx_inventory_mutex, return relay->m_send_mempool));
    BOOST_CHECK_EQUAL(WITH_LOCK(relay->m_tx_inventory_mutex, return relay->m_last_inv_sequence), 1U);
    BOOST_CHECK_EQUAL(relay->m_fee_filter_received.load(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
