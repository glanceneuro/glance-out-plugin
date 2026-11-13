// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

#ifndef LHTR_CLIENT_H_INCLUDED
#define LHTR_CLIENT_H_INCLUDED

// juce_core on its own, not ProcessorHeaders.h. All this class needs is
// juce::Thread and juce::String, and pulling in the whole GUI would drag
// graphics and OpenGL into a UDP client -- and would stop
// test/run_loopback_test.sh from linking it against a stand-in broker without
// building Open Ephys. AppConfig.h first is what picks up the project's JUCE
// settings; it is the same thing JuceHeader.h does, just narrower.
#include <AppConfig.h>
#include <juce_core/juce_core.h>

#include "LhtrProtocol.h"

#include <atomic>
#include <cstdint>

/**
    Sender side of the LaserHAT network trigger (LHTR).

    The wire format lives in LhtrProtocol.h, which is kept honest against
    LaserDriver/Pi/udp_trigger.py by test/run_protocol_test.sh. This class is
    only the socket and the thread around it.

    The broker (`laserhat-brokerd --udp HOST:PORT`) raises Pi GPIO 26 -> MCU
    PA27 as soon as a trigger datagram arrives, then ACKs. Pulse width and
    energy are the MCU's business, not ours; we only say "now".

    THE AUDIO THREAD NEVER WAITS. fire() does one send() on an already
    connected, non-blocking UDP socket and returns -- no name lookup, no ACK
    wait, no allocation, no lock. ACKs are collected by a separate listener
    thread that only updates atomic counters for the UI, so a Pi that is slow,
    busy or absent can never stall acquisition.

    Delivery is AT-MOST-ONCE, which is the protocol's choice and the right one
    here: a late pulse is worse than a missing one, so nothing is retransmitted.
*/
class LhtrClient : private juce::Thread
{
public:
    LhtrClient();
    ~LhtrClient() override;

    /** Resolve `host`, open the socket, and start the ACK listener.

        Call from the message thread only -- it may block for a DNS lookup.
        Returns false on failure, with getLastError() set. */
    bool open (const juce::String& host, int port);

    /** Stop the listener and close the socket. Safe to call when not open. */
    void close();

    bool isOpen() const { return socketFd >= 0; }

    /** Why the last open() failed. Empty if it did not. */
    juce::String getLastError() const { return lastError; }

    /** The peer we actually resolved to, for the UI. */
    juce::String getPeerAddress() const { return peerAddress; }

    /** Send one TRIGGER. Safe on the audio thread: one non-blocking send().
        Returns false only if the socket is closed or send() failed outright. */
    bool fire();

    /** Send one PING. The broker answers without touching the laser, so this is
        the safe way to prove the path end to end. */
    bool ping();

    // ---- counters, for the editor. Updated by the listener thread. ----
    uint32_t getSent() const { return sent.load (std::memory_order_relaxed); }
    uint32_t getAcked() const { return acked.load (std::memory_order_relaxed); }
    uint32_t getFired() const { return firedCount.load (std::memory_order_relaxed); }
    uint32_t getMcuBusy() const { return mcuBusy.load (std::memory_order_relaxed); }
    uint32_t getSendErrors() const { return sendErrors.load (std::memory_order_relaxed); }

    /** Round trip for the most recent ACK, microseconds. 0 if none yet. */
    uint32_t getLastRttUs() const { return lastRttUs.load (std::memory_order_relaxed); }

    /** In-Pi latency for the most recent ACK: packet arrival to GPIO edge,
        microseconds. This is the broker's own measurement, not ours. */
    uint32_t getLastPiUs() const { return lastPiUs.load (std::memory_order_relaxed); }

    /** Last broker status as text, for the UI. */
    juce::String getLastStatus() const;

    /** True once any ACK has come back, i.e. the broker is demonstrably there. */
    bool hasHeardFromBroker() const { return acked.load (std::memory_order_relaxed) > 0; }

    void resetCounters();

private:
    void run() override; // ACK listener

    /** Send one request of the given type. Shared by fire() and ping(). */
    bool sendRequest (uint8_t msgType);

    /** Monotonic microseconds, echoed by the broker so we can measure RTT. */
    static uint64_t nowMicros();

    int socketFd = -1;
    uint32_t seqCounter = 0;
    juce::String lastError;
    juce::String peerAddress;

    std::atomic<uint32_t> sent { 0 };
    std::atomic<uint32_t> acked { 0 };
    std::atomic<uint32_t> firedCount { 0 };
    std::atomic<uint32_t> mcuBusy { 0 };
    std::atomic<uint32_t> sendErrors { 0 };
    std::atomic<uint32_t> lastRttUs { 0 };
    std::atomic<uint32_t> lastPiUs { 0 };
    std::atomic<int> lastStatus { -1 };
    std::atomic<uint8_t> lastFlags { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LhtrClient)
};

#endif // LHTR_CLIENT_H_INCLUDED
