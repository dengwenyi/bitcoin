// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/transport_channel.h>
#include <node/transport_factory.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(transport_factory_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(selects_channel_protocol)
{
    auto v1{node::MakeTransportChannel(/*id=*/1, /*use_v2transport=*/false, /*inbound=*/false)};
    BOOST_REQUIRE(v1 != nullptr);
    BOOST_CHECK(v1->GetInfo().transport_type == TransportProtocolType::V1);

    auto v2_outbound{node::MakeTransportChannel(/*id=*/2, /*use_v2transport=*/true, /*inbound=*/false)};
    BOOST_REQUIRE(v2_outbound != nullptr);
    BOOST_CHECK(v2_outbound->GetInfo().transport_type == TransportProtocolType::DETECTING);

    auto v2_inbound{node::MakeTransportChannel(/*id=*/3, /*use_v2transport=*/true, /*inbound=*/true)};
    BOOST_REQUIRE(v2_inbound != nullptr);
    BOOST_CHECK(v2_inbound->GetInfo().transport_type == TransportProtocolType::DETECTING);
}

BOOST_AUTO_TEST_SUITE_END()
