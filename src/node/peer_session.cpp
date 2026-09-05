// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/peer_session.h>

#include <headerssync.h>

#include <utility>

namespace node {

PeerSession::PeerSession(NodeId id, ServiceFlags our_services, bool is_inbound)
    : m_id{id}, m_our_services{our_services}, m_is_inbound{is_inbound}
{
}

PeerSession::~PeerSession() = default;

void PeerSession::MarkForDiscouragement()
{
    LOCK(m_misbehavior_mutex);
    m_should_discourage = true;
}

bool PeerSession::ConsumeShouldDiscourage()
{
    LOCK(m_misbehavior_mutex);
    return std::exchange(m_should_discourage, false);
}

bool PeerSession::HasGetDataRequests() const
{
    LOCK(m_getdata_requests_mutex);
    return !m_getdata_requests.empty();
}

} // namespace node
