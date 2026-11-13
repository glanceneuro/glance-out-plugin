// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

#ifndef LASERHAT_OUTPUT_H_INCLUDED
#define LASERHAT_OUTPUT_H_INCLUDED

#include <ProcessorHeaders.h>

#include "LhtrClient.h"

#include <atomic>

/**
    Turns a TTL event into a laser pulse.

    One UDP datagram per rising edge on the selected line, to the LaserHAT
    broker, which raises the MCU's trigger pin. The MCU owns pulse width and
    energy -- this plugin only says "now", so there is deliberately no pulse
    duration control here.

    Put any event source upstream: Crossing Detector for a simple threshold,
    or a ripple/phase detector for something cleverer. The sink does not care
    where the event came from.

    @see GenericProcessor
*/
class LaserHatOutput : public GenericProcessor
{
public:
    LaserHatOutput();
    ~LaserHatOutput() override;

    void registerParameters() override;

    /** Keeps the cached minimum interval in step with the parameter. */
    void parameterValueChanged (Parameter* param) override;

    /** Looks for events; everything else happens in handleTTLEvent. */
    void process (AudioBuffer<float>& buffer) override;

    /** Fires the laser on a rising edge of the input line, if the gate is open
        and the minimum interval has elapsed. */
    void handleTTLEvent (TTLEventPtr event) override;

    void updateSettings() override;

    /** Clears the per-run counters so the editor's numbers describe this run. */
    bool startAcquisition() override;

    AudioProcessorEditor* createEditor() override;

    /** Open the socket to the broker. Message thread only. */
    bool connectToBroker();

    /** Close the socket. Safe when not connected. */
    void disconnectFromBroker();

    bool isConnectedToBroker() const { return client.isOpen(); }

    /** Human-readable connection state for the editor. */
    String getConnectionStatus() const { return connectionStatus; }

    LhtrClient& getClient() { return client; }

    /** Triggers suppressed by the minimum-interval guard this run. */
    uint32_t getSuppressedCount() const { return suppressed.load (std::memory_order_relaxed); }

private:
    LhtrClient client;

    String connectionStatus { "Disconnected" };

    /** Open unless a gate line is selected and currently low. */
    bool gateIsOpen = true;

    /** Minimum spacing between datagrams, in high-resolution ticks. Cached from
        the parameter so the audio thread does not touch a var. Zero = off. */
    std::atomic<int64> minIntervalTicks { 0 };

    /** When we last sent a trigger. Audio thread only. */
    int64 lastFireTicks = 0;

    std::atomic<uint32_t> suppressed { 0 };

    /** Recompute minIntervalTicks from the parameter's current value. */
    void cacheMinInterval();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaserHatOutput)
};

#endif // LASERHAT_OUTPUT_H_INCLUDED
