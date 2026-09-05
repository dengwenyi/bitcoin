// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/peer_tx_relay.h>

#include <util/check.h>

namespace node {

PeerTxRelayState::~PeerTxRelayState() = default;

PeerTxRelay* PeerTxRelayState::SetTxRelay()
{
    LOCK(m_tx_relay_mutex);
    Assume(!m_tx_relay);
    m_tx_relay = std::make_unique<PeerTxRelay>();
    return m_tx_relay.get();
}

PeerTxRelay* PeerTxRelayState::GetTxRelay()
{
    return WITH_LOCK(m_tx_relay_mutex, return m_tx_relay.get());
}

} // namespace node
