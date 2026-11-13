// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

// Live-socket check for LhtrClient, against a stand-in broker on localhost.
//
// test_protocol.cpp proves the bytes are right. This proves the socket around
// them is right, which is where the subtler mistakes live:
//
//   * a host given by NAME resolves (inet_pton would reject "localhost";
//     getaddrinfo is why that works)
//   * fire() never blocks, even with nothing listening
//   * the ACK listener actually parses replies and moves the counters
//   * PING is answered without the broker treating it as a trigger
//   * sequence numbers advance, so the broker's duplicate check does not
//     swallow the second pulse
//
// The peer is test/fake_broker.py, which decodes with the real
// LaserDriver/Pi/udp_trigger.py when it can find it. Driven by
// test/run_loopback_test.sh; no Pi and no laser involved.

#include "../Source/LhtrClient.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void check (bool cond, const char* what)
{
    std::printf ("  %-4s %s\n", cond ? "ok" : "FAIL", what);
    if (! cond)
        ++failures;
}

/** Wait up to timeoutMs for the ACK count to reach `want`. */
static bool waitForAcks (const LhtrClient& c, uint32_t want, int timeoutMs)
{
    for (int waited = 0; waited < timeoutMs; waited += 5)
    {
        if (c.getAcked() >= want)
            return true;
        juce::Thread::sleep (5);
    }
    return false;
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: test_loopback <port>\n");
        return 2;
    }

    const int port = std::atoi (argv[1]);

    // --- a host that only getaddrinfo can resolve -------------------------
    {
        LhtrClient client;
        check (client.open ("localhost", port),
               "opens to a NAME, not just a dotted quad");
        check (client.isOpen(), "reports itself open");
        check (client.getPeerAddress().startsWith ("127.0.0.1:"),
               "resolved localhost to 127.0.0.1");

        // --- ping is answered, and is not a trigger -----------------------
        check (client.ping(), "ping() sends");
        check (waitForAcks (client, 1, 2000), "the broker answered the ping");
        check (client.getLastStatus().startsWith ("pong"),
               "a ping comes back PONG");
        check (client.getFired() == 0, "a ping did NOT fire the laser");

        // --- a trigger does fire ------------------------------------------
        check (client.fire(), "fire() sends");
        check (waitForAcks (client, 2, 2000), "the broker answered the trigger");
        check (client.getLastStatus().startsWith ("fired"),
               "a trigger comes back FIRED");
        check (client.getFired() == 1, "one fire counted");

        // --- a second trigger is not mistaken for a repeat of the first ---
        // The broker drops a seq it has already seen. If seq did not advance,
        // this would come back DUPLICATE and the laser would stay dark.
        check (client.fire(), "a second fire() sends");
        check (waitForAcks (client, 3, 2000), "the broker answered it");
        check (client.getLastStatus().startsWith ("fired"),
               "the second trigger is NOT a duplicate");
        check (client.getFired() == 2, "two fires counted");

        check (client.getSendErrors() == 0, "no send errors");
        check (client.getSent() == 3, "sent 3 datagrams (1 ping + 2 triggers)");
        check (client.getLastRttUs() > 0, "a round trip was measured");

        client.close();
        check (! client.isOpen(), "closes cleanly");
    }

    // --- nothing listening: fire() must still not block or fail -----------
    // Port 1 has no broker. UDP will not tell us so, and that is the point:
    // acquisition must not care whether the Pi is there.
    {
        LhtrClient client;
        if (client.open ("127.0.0.1", 1))
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 100; ++i)
                client.fire();
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;

            check (elapsed < 100.0, "100 fires into the void took under 100 ms");
            check (client.getAcked() == 0, "nothing answered, as expected");
            client.close();
        }
        else
        {
            std::printf ("  skip  no-listener case (could not open socket)\n");
        }
    }

    // --- a host that cannot resolve fails, and says so --------------------
    {
        LhtrClient client;
        check (! client.open ("no-such-host.invalid", port),
               "an unresolvable host fails to open");
        check (client.getLastError().isNotEmpty(), "and reports why");
        check (! client.isOpen(), "and is not left half-open");
    }

    if (failures > 0)
    {
        std::printf ("TB_FAIL  Errors: %d\n", failures);
        return 1;
    }
    std::printf ("TB_PASS  Errors: 0\n");
    return 0;
}
