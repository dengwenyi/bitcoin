// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_TRANSPORT_FACTORY_H
#define BITCOIN_NODE_TRANSPORT_FACTORY_H

#include <node/transport_channel.h>

#include <cstdint>
#include <memory>

namespace node {

/** Create the configured V1 or V2 channel for one inbound or outbound connection. */
std::unique_ptr<TransportChannel> MakeTransportChannel(int64_t node_id, bool use_v2transport, bool inbound) noexcept;

} // namespace node

#endif // BITCOIN_NODE_TRANSPORT_FACTORY_H
