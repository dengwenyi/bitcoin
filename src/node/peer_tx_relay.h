// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_PEER_TX_RELAY_H
#define BITCOIN_NODE_PEER_TX_RELAY_H

#include <common/bloom.h>
#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <sync.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace node {

/** Transaction-relay state for one peer, synchronized independently by concern. */
struct PeerTxRelay {
    mutable RecursiveMutex m_bloom_filter_mutex;
    /** Whether we relay transactions to this peer. */
    bool m_relay_txs GUARDED_BY(m_bloom_filter_mutex){false};
    /** A bloom filter for transaction announcements to this peer. */
    std::unique_ptr<CBloomFilter> m_bloom_filter PT_GUARDED_BY(m_bloom_filter_mutex) GUARDED_BY(m_bloom_filter_mutex){nullptr};

    mutable RecursiveMutex m_tx_inventory_mutex;
    /** Transaction identifiers already announced in either direction. */
    CRollingBloomFilter m_tx_inventory_known_filter GUARDED_BY(m_tx_inventory_mutex){50000, 0.000001};
    /** Transaction identifiers awaiting announcement. */
    std::vector<Wtxid> m_tx_inventory_to_send GUARDED_BY(m_tx_inventory_mutex);
    /** Whether the peer requested a complete mempool announcement. */
    bool m_send_mempool GUARDED_BY(m_tx_inventory_mutex){false};
    /** Next transaction inventory announcement time. */
    std::chrono::microseconds m_next_inv_send_time GUARDED_BY(m_tx_inventory_mutex){0};
    /** Mempool sequence used by the last inventory announcement. */
    uint64_t m_last_inv_sequence GUARDED_BY(m_tx_inventory_mutex){1};

    /** Minimum fee rate accepted in announcements by this peer. */
    std::atomic<CAmount> m_fee_filter_received{0};
};

/** Owns the optional transaction-relay state attached to one peer. */
class PeerTxRelayState
{
public:
    PeerTxRelayState() = default;
    ~PeerTxRelayState();

    PeerTxRelay* SetTxRelay() EXCLUSIVE_LOCKS_REQUIRED(!m_tx_relay_mutex);
    PeerTxRelay* GetTxRelay() EXCLUSIVE_LOCKS_REQUIRED(!m_tx_relay_mutex);

private:
    mutable Mutex m_tx_relay_mutex;
    std::unique_ptr<PeerTxRelay> m_tx_relay GUARDED_BY(m_tx_relay_mutex);
};

} // namespace node

#endif // BITCOIN_NODE_PEER_TX_RELAY_H
