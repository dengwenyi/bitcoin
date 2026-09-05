// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/transport_factory.h>

#include <net.h>

namespace node {

std::unique_ptr<TransportChannel> MakeTransportChannel(int64_t node_id, bool use_v2transport, bool inbound) noexcept
{
    if (use_v2transport) {
        return std::make_unique<V2Transport>(node_id, /*initiating=*/!inbound);
    }
    return std::make_unique<V1Transport>(node_id);
}

} // namespace node
