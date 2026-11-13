// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

#ifndef LHTR_PROTOCOL_H_INCLUDED
#define LHTR_PROTOCOL_H_INCLUDED

// The LaserHAT network trigger wire format, and nothing else.
//
// Deliberately free of JUCE and of any socket header, so the encoding can be
// tested on its own against the reference implementation
// (LaserDriver/Pi/udp_trigger.py) without building a plugin or opening a
// socket. test/test_protocol.cpp does exactly that.
//
// Mirrors Pi/brokerd/udp_trigger.h. Keep the three in step: a silent
// disagreement here is a laser that never fires, or worse, one that does.

#include <cstdint>
#include <cstddef>

namespace lhtr
{

constexpr uint32_t MAGIC = 0x5254484Cu; // "LHTR" read little-endian
constexpr uint8_t VERSION = 1;
constexpr int DEFAULT_PORT = 17017;

constexpr uint8_t TYPE_TRIGGER = 0x01;
constexpr uint8_t TYPE_PING = 0x02;
constexpr uint8_t TYPE_ACK = 0x81;

// Python: _REQ = struct.Struct("<IBBHIQ")   magic ver type flags seq client_ts
constexpr size_t REQUEST_BYTES = 20;
// Python: _REP = struct.Struct("<IBBBBIQQQ")
//         magic ver type status flags seq client_ts rx_ns edge_ns
constexpr size_t REPLY_BYTES = 36;

enum Status : uint8_t
{
    FIRED = 0,
    DUPLICATE = 1,
    PONG = 2,
    BAD_VERSION = 3,
    BAD_TYPE = 4,
    NO_GPIO = 5
};

constexpr uint8_t F_MCU_BUSY = 0x01;   // mid-pulse: the MCU ignored this edge
constexpr uint8_t F_MCU_DOWN = 0x02;   // broker has no recent MCU status
constexpr uint8_t F_GPIO_SIM = 0x04;   // simulated GPIO (off-hardware test)
constexpr uint8_t F_RX_TS_USER = 0x08; // rx_ns is a userspace timestamp

// Byte-at-a-time little-endian accessors. Not a packed-struct cast: the wire
// layout must not depend on this compiler's padding or this host's endianness.
inline void putU16 (uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t) (v & 0xFF);
    p[1] = (uint8_t) ((v >> 8) & 0xFF);
}

inline void putU32 (uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (uint8_t) ((v >> (8 * i)) & 0xFF);
}

inline void putU64 (uint8_t* p, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        p[i] = (uint8_t) ((v >> (8 * i)) & 0xFF);
}

inline uint32_t getU32 (const uint8_t* p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16)
           | ((uint32_t) p[3] << 24);
}

inline uint64_t getU64 (const uint8_t* p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}

/** Write a 20-byte request. Returns bytes written. */
inline size_t packRequest (uint8_t* buf, uint8_t msgType, uint32_t seq, uint64_t clientTs)
{
    putU32 (buf + 0, MAGIC);
    buf[4] = VERSION;
    buf[5] = msgType;
    putU16 (buf + 6, 0); // flags: the sender sets none
    putU32 (buf + 8, seq);
    putU64 (buf + 12, clientTs);
    return REQUEST_BYTES;
}

struct Reply
{
    uint8_t status = 0;
    uint8_t flags = 0;
    uint32_t seq = 0;
    uint64_t clientTs = 0;
    uint64_t rxNs = 0;
    uint64_t edgeNs = 0;

    /** Broker-measured time from packet arrival to GPIO edge, nanoseconds. */
    uint64_t piLatencyNs() const { return edgeNs > rxNs ? edgeNs - rxNs : 0; }
};

/** Parse a reply. Returns false if it is too short, not ours, or not an ACK. */
inline bool parseReply (const uint8_t* buf, size_t len, Reply& out)
{
    if (len < REPLY_BYTES)
        return false;
    if (getU32 (buf) != MAGIC || buf[5] != TYPE_ACK)
        return false;

    out.status = buf[6];
    out.flags = buf[7];
    out.seq = getU32 (buf + 8);
    out.clientTs = getU64 (buf + 12);
    out.rxNs = getU64 (buf + 20);
    out.edgeNs = getU64 (buf + 28);
    return true;
}

} // namespace lhtr

#endif // LHTR_PROTOCOL_H_INCLUDED
