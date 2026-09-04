// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_PEER_SESSION_H
#define BITCOIN_NODE_PEER_SESSION_H

#include <node/eviction.h>
#include <protocol.h>
#include <sync.h>
#include <uint256.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

namespace node {

/** Identity and independently synchronized state for one P2P peer session. */
struct PeerSession {
    struct BlockAnnouncements {
        /** Blocks queued for immediate inv relay, in send order. */
        std::vector<uint256> m_blocks_for_inv_relay;
        /** Blocks preferred for headers relay, with inv fallback. */
        std::vector<uint256> m_blocks_for_headers_relay;
        /** Last inventory hash whose request should trigger the next batch. */
        uint256 m_continuation_block;
    };

    /** Same id as the CNode object for this peer. */
    const NodeId m_id;

    /** Services offered to this peer when the session was initialized. */
    const ServiceFlags m_our_services;

    /** Whether this session was accepted as an inbound connection. */
    const bool m_is_inbound;

    /** Services this peer offered to us. */
    std::atomic<ServiceFlags> m_their_services{NODE_NONE};

    /** Pong nonce and send time, plus an explicit user ping request. */
    std::atomic<uint64_t> m_ping_nonce_sent{0};
    std::atomic<NodeClock::time_point> m_ping_start{NodeClock::epoch};
    std::atomic<bool> m_ping_queued{false};

    /** Capabilities negotiated independently of the message-processing lock. */
    std::atomic<bool> m_wtxid_relay{false};
    std::atomic_bool m_addr_relay_enabled{false};
    std::atomic_bool m_wants_addrv2{false};
    std::atomic<bool> m_sent_sendheaders{false};

    /** Address processing counters exposed through peer statistics. */
    std::atomic<uint64_t> m_addr_rate_limited{0};
    std::atomic<uint64_t> m_addr_processed{0};

    /** Offset derived from the peer's VERSION timestamp. */
    std::atomic<std::chrono::seconds> m_time_offset{std::chrono::seconds{0}};

    PeerSession(NodeId id, ServiceFlags our_services, bool is_inbound);

    /** Mark this session for disconnect and address discouragement. */
    void MarkForDiscouragement();

    /** Consume a pending discouragement decision exactly once. */
    bool ConsumeShouldDiscourage();

    /** Run one operation while holding the block-announcement lock. */
    template <typename Callable>
    void WithBlockAnnouncements(Callable&& callback)
    {
        LOCK(m_block_inv_mutex);
        callback(m_block_announcements);
    }

private:
    Mutex m_misbehavior_mutex;
    bool m_should_discourage GUARDED_BY(m_misbehavior_mutex){false};

    Mutex m_block_inv_mutex;
    BlockAnnouncements m_block_announcements GUARDED_BY(m_block_inv_mutex);
};

} // namespace node

#endif // BITCOIN_NODE_PEER_SESSION_H
