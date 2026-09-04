// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/peer_session.h>

namespace node {

PeerSession::PeerSession(NodeId id, ServiceFlags our_services, bool is_inbound)
    : m_id{id}, m_our_services{our_services}, m_is_inbound{is_inbound}
{
}

} // namespace node
