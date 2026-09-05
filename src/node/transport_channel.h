// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_TRANSPORT_CHANNEL_H
#define BITCOIN_NODE_TRANSPORT_CHANNEL_H

#include <node/connection_types.h>
#include <uint256.h>
#include <util/time.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tuple>

class CNetMessage;
struct CSerializedNetMsg;

namespace node {

/** Bidirectional message/wire-byte channel used by one network connection. */
class TransportChannel
{
public:
    virtual ~TransportChannel() = default;

    struct Info {
        TransportProtocolType transport_type;
        std::optional<uint256> session_id;
    };

    virtual Info GetInfo() const noexcept = 0;

    /** Return whether a complete protocol-agnostic message is available. */
    virtual bool ReceivedMessageComplete() const = 0;

    /** Consume valid wire bytes from the front of the supplied span. */
    virtual bool ReceivedBytes(std::span<const uint8_t>& msg_bytes) = 0;

    /** Move one completed message out of the receive side. */
    virtual CNetMessage GetReceivedMessage(NodeClock::time_point time, bool& reject_message) = 0;

    /** Enqueue the next protocol message when the send side can accept it. */
    virtual bool SetMessageToSend(CSerializedNetMsg& msg) noexcept = 0;

    using BytesToSend = std::tuple<
        std::span<const uint8_t> /*to_send*/,
        bool /*more*/,
        const std::string& /*message_type*/
    >;

    /** Expose the current immutable wire-byte window and continuation hint. */
    virtual BytesToSend GetBytesToSend(bool have_next_message) const noexcept = 0;

    /** Advance the send window by the number of bytes accepted by the socket. */
    virtual void MarkBytesSent(size_t bytes_sent) noexcept = 0;

    virtual size_t GetSendMemoryUsage() const noexcept = 0;
    virtual bool ShouldReconnectV1() const noexcept = 0;
};

} // namespace node

#endif // BITCOIN_NODE_TRANSPORT_CHANNEL_H
