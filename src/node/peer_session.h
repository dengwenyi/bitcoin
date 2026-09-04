// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_PEER_SESSION_H
#define BITCOIN_NODE_PEER_SESSION_H

#include <node/eviction.h>
#include <protocol.h>

namespace node {

/** Immutable identity and negotiated-local context for one P2P peer session. */
struct PeerSession {
    /** Same id as the CNode object for this peer. */
    const NodeId m_id;

    /** Services offered to this peer when the session was initialized. */
    const ServiceFlags m_our_services;

    /** Whether this session was accepted as an inbound connection. */
    const bool m_is_inbound;

    PeerSession(NodeId id, ServiceFlags our_services, bool is_inbound);
};

} // namespace node

#endif // BITCOIN_NODE_PEER_SESSION_H
