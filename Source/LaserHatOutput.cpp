// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

#include "LaserHatOutput.h"
#include "LaserHatOutputEditor.h"

LaserHatOutput::LaserHatOutput()
    : GenericProcessor ("LaserHAT Output")
{
}

LaserHatOutput::~LaserHatOutput()
{
    disconnectFromBroker();
}

void LaserHatOutput::registerParameters()
{
    addStringParameter (Parameter::PROCESSOR_SCOPE, "hostname", "Host",
                        "Hostname or IP address of the Pi running laserhat-brokerd",
                        "laserhat.local", true);

    addIntParameter (Parameter::PROCESSOR_SCOPE, "port", "Port",
                     "UDP port the broker listens on",
                     lhtr::DEFAULT_PORT, 1, 65535, true);

    // The laser's own limit, not the detector's. Upstream lockout stops a
    // single burst being counted twice; this stops any combination of upstream
    // events -- or a stray TTL line -- driving the laser harder than it should
    // be driven. Keep both.
    addIntParameter (Parameter::PROCESSOR_SCOPE, "min_interval", "Min interval (ms)",
                     "Shortest time between pulses. Extra triggers are dropped. 0 = no limit",
                     0, 0, 10000);

    addIntParameter (Parameter::STREAM_SCOPE, "input_line", "Trigger line",
                     "TTL line that fires the laser, on its rising edge", 1, 1, 16);

    addIntParameter (Parameter::STREAM_SCOPE, "gate_line", "Gate line",
                     "TTL line that must be high to allow firing. 0 = always allow", 0, 0, 16);
}

AudioProcessorEditor* LaserHatOutput::createEditor()
{
    editor = std::make_unique<LaserHatOutputEditor> (this);
    return editor.get();
}

bool LaserHatOutput::connectToBroker()
{
    const String host = getParameter ("hostname")->getValue().toString();
    const int port = (int) getParameter ("port")->getValue();

    LOGC ("Opening LHTR socket to ", host, ":", port);

    if (client.open (host, port))
    {
        connectionStatus = "Open to " + client.getPeerAddress();
        LOGC ("LHTR socket open to ", client.getPeerAddress());

        // UDP open() proves nothing about the far end, so say hello. PING is
        // answered by the broker without touching the laser, which makes it
        // the one safe way to test the path.
        client.ping();

        CoreServices::sendStatusMessage ("LaserHAT: socket open to " + client.getPeerAddress());
    }
    else
    {
        connectionStatus = "Error: " + client.getLastError();
        LOGC ("LHTR socket failed: ", client.getLastError());
        CoreServices::sendStatusMessage ("LaserHAT: " + client.getLastError());
    }

    CoreServices::updateSignalChain (this);
    return client.isOpen();
}

void LaserHatOutput::disconnectFromBroker()
{
    if (! client.isOpen())
        return;

    client.close();
    connectionStatus = "Disconnected";
    LOGC ("LHTR socket closed");
    CoreServices::sendStatusMessage ("LaserHAT: socket closed");
    CoreServices::updateSignalChain (this);
}

void LaserHatOutput::updateSettings()
{
    isEnabled = client.isOpen();
    cacheMinInterval();
}

void LaserHatOutput::cacheMinInterval()
{
    auto* param = getParameter ("min_interval");
    if (param == nullptr)
        return;

    const int ms = (int) param->getValue();
    const int64 ticks = ms > 0
                            ? (Time::getHighResolutionTicksPerSecond() * ms) / 1000
                            : 0;
    minIntervalTicks.store (ticks, std::memory_order_relaxed);
}

void LaserHatOutput::parameterValueChanged (Parameter* param)
{
    const String name = param->getName();

    if (name.equalsIgnoreCase ("min_interval"))
    {
        cacheMinInterval();
    }
    else if (name.equalsIgnoreCase ("gate_line"))
    {
        // Line 0 means "no gate". Any other line starts closed and opens on the
        // first rising edge, so a gate that is never driven blocks rather than
        // fires -- the safe direction for a laser.
        gateIsOpen = (int (param->getValue()) == 0);
    }
}

bool LaserHatOutput::startAcquisition()
{
    client.resetCounters();
    suppressed.store (0, std::memory_order_relaxed);
    lastFireTicks = 0;
    return true;
}

void LaserHatOutput::process (AudioBuffer<float>&)
{
    // A sink that reads no samples: everything happens in handleTTLEvent,
    // which checkForEvents() calls for each event in this block.
    checkForEvents();
}

void LaserHatOutput::handleTTLEvent (TTLEventPtr event)
{
    if (! client.isOpen())
        return;

    DataStream* stream = getDataStream (event->getStreamId());
    if (stream == nullptr)
        return;

    const int eventBit = event->getLine() + 1; // parameters are 1-based

    if (eventBit == int ((*stream)["gate_line"]))
        gateIsOpen = event->getState();

    if (! gateIsOpen)
        return;

    if (eventBit != int ((*stream)["input_line"]))
        return;

    if (! event->getState()) // rising edges only
        return;

    // The guard is in wall-clock time, measured between datagrams actually
    // sent, because that is what the laser experiences. Sample time would not
    // do: every event in one process() block is handled microseconds apart
    // regardless of how far apart the samples were, so a burst that looks
    // well-spaced in the data can still arrive at the laser as a burst.
    const int64 now = Time::getHighResolutionTicks();
    const int64 minTicks = minIntervalTicks.load (std::memory_order_relaxed);

    if (minTicks > 0 && lastFireTicks != 0 && (now - lastFireTicks) < minTicks)
    {
        suppressed.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    if (client.fire())
        lastFireTicks = now;
}
