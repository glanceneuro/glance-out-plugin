// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

// Wire-format check for the LaserHAT trigger protocol.
//
// The reference implementation is LaserDriver/Pi/udp_trigger.py, which is
// itself a mirror of Pi/brokerd/udp_trigger.h -- the C the broker actually
// runs. This test emits bytes from OUR encoder and the runner diffs them
// against bytes from THAT Python, so the two cannot drift apart silently.
//
// Worth testing precisely because the failure is invisible: a field at the
// wrong offset does not crash, it just means the laser never fires -- or
// fires when it should not.
//
// Build and run via test/run_protocol_test.sh (no JUCE, no GUI, no Pi).

// Windows pulls in a header (via JUCE) that defines DUPLICATE as an
// object-like macro. That turned `lhtr::DUPLICATE` into `lhtr::<number>` and
// broke the build on the Windows runner alone. Poison the name here so a
// reintroduced ALL_CAPS member fails on every platform instead of only there.
#define DUPLICATE 6

#include "../Source/LhtrProtocol.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;

static void check (bool cond, const char* what)
{
    if (! cond)
    {
        std::printf ("  FAIL %s\n", what);
        ++failures;
    }
}

static std::string hex (const uint8_t* p, size_t n)
{
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i)
    {
        s += d[p[i] >> 4];
        s += d[p[i] & 0xF];
    }
    return s;
}

int main()
{
    // --- sizes, straight from the Python struct formats -------------------
    check (lhtr::REQUEST_BYTES == 20, "request is 20 bytes (\"<IBBHIQ\")");
    check (lhtr::REPLY_BYTES == 36, "reply is 36 bytes (\"<IBBBBIQQQ\")");

    // --- magic is the ASCII "LHTR" read little-endian ---------------------
    uint8_t m[4];
    lhtr::putU32 (m, lhtr::MAGIC);
    check (m[0] == 'L' && m[1] == 'H' && m[2] == 'T' && m[3] == 'R',
           "magic serialises to the bytes 'LHTR'");

    // --- encode the vectors the runner also asks Python to encode ---------
    struct Case
    {
        uint8_t type;
        uint32_t seq;
        uint64_t ts;
    };
    const std::vector<Case> cases = {
        { lhtr::TYPE_TRIGGER, 0u, 0ull },
        { lhtr::TYPE_TRIGGER, 1u, 1ull },
        { lhtr::TYPE_PING, 42u, 1234567890ull },
        { lhtr::TYPE_TRIGGER, 0xFFFFFFFFu, 0xFFFFFFFFFFFFFFFFull }, // wraparound
        { lhtr::TYPE_TRIGGER, 0x12345678u, 0x0123456789ABCDEFull }, // byte order
    };

    std::printf ("CPP_VECTORS\n");
    for (const auto& c : cases)
    {
        uint8_t buf[lhtr::REQUEST_BYTES];
        std::memset (buf, 0xAA, sizeof (buf)); // poison: every byte must be written
        const size_t n = lhtr::packRequest (buf, c.type, c.seq, c.ts);
        check (n == lhtr::REQUEST_BYTES, "packRequest returned the full length");
        std::printf ("%u %u %llu %s\n",
                     (unsigned) c.type,
                     (unsigned) c.seq,
                     (unsigned long long) c.ts,
                     hex (buf, n).c_str());
    }
    std::printf ("END\n");

    // --- round trip a reply we build by hand ------------------------------
    {
        uint8_t r[lhtr::REPLY_BYTES];
        std::memset (r, 0, sizeof (r));
        lhtr::putU32 (r + 0, lhtr::MAGIC);
        r[4] = lhtr::VERSION;
        r[5] = lhtr::TYPE_ACK;
        r[6] = (uint8_t) lhtr::Status::Fired;
        r[7] = lhtr::F_GPIO_SIM;
        lhtr::putU32 (r + 8, 0xDEADBEEFu);
        lhtr::putU64 (r + 12, 111111ull);
        lhtr::putU64 (r + 20, 2000ull);
        lhtr::putU64 (r + 28, 2500ull);

        lhtr::Reply rep;
        check (lhtr::parseReply (r, sizeof (r), rep), "a well-formed ACK parses");
        check (rep.is (lhtr::Status::Fired), "status survives the round trip");
        check (rep.flags == lhtr::F_GPIO_SIM, "flags survive");
        check (rep.seq == 0xDEADBEEFu, "seq survives");
        check (rep.clientTs == 111111ull, "client_ts survives");
        check (rep.piLatencyNs() == 500ull, "in-Pi latency is edge_ns - rx_ns");
    }

    // --- things that must be REJECTED, not silently accepted --------------
    {
        uint8_t r[lhtr::REPLY_BYTES];
        std::memset (r, 0, sizeof (r));
        lhtr::putU32 (r + 0, lhtr::MAGIC);
        r[5] = lhtr::TYPE_ACK;
        lhtr::Reply rep;

        check (! lhtr::parseReply (r, lhtr::REPLY_BYTES - 1, rep),
               "a short datagram is rejected");

        lhtr::putU32 (r + 0, 0xDEADBEEFu);
        check (! lhtr::parseReply (r, sizeof (r), rep),
               "a foreign magic is rejected");

        lhtr::putU32 (r + 0, lhtr::MAGIC);
        r[5] = lhtr::TYPE_TRIGGER; // our own request echoed back, not an ACK
        check (! lhtr::parseReply (r, sizeof (r), rep),
               "a non-ACK message type is rejected");
    }

    if (failures > 0)
    {
        std::printf ("TB_FAIL  Errors: %d\n", failures);
        return 1;
    }
    std::printf ("TB_PASS  Errors: 0\n");
    return 0;
}
