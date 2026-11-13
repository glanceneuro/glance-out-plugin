// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

#include "LaserHatOutputEditor.h"

LaserHatOutputEditor::LaserHatOutputEditor (GenericProcessor* parentNode)
    : GenericEditor (parentNode)
{
    desiredWidth = 340;

    // left column: where to send
    addTextBoxParameterEditor (Parameter::PROCESSOR_SCOPE, "hostname", 10, 29);
    addTextBoxParameterEditor (Parameter::PROCESSOR_SCOPE, "port", 10, 54);

    connectButton = std::make_unique<UtilityButton> ("OPEN");
    connectButton->setBounds (10, 84, 75, 20);
    connectButton->addListener (this);
    addAndMakeVisible (connectButton.get());

    // Hidden until the socket is open: a ping with nowhere to go is just a
    // confusing no-op.
    pingButton = std::make_unique<UtilityButton> ("PING");
    pingButton->setBounds (90, 84, 70, 20);
    pingButton->addListener (this);
    addChildComponent (pingButton.get());

    statusLabel = std::make_unique<Label> ("Status", "Disconnected");
    statusLabel->setBounds (8, 106, 160, 18);
    statusLabel->setFont (FontOptions (11.0f));
    statusLabel->setColour (Label::textColourId, Colours::grey);
    addAndMakeVisible (statusLabel.get());

    // right column: when to send
    addComboBoxParameterEditor (Parameter::STREAM_SCOPE, "input_line", 175, 29);
    addComboBoxParameterEditor (Parameter::STREAM_SCOPE, "gate_line", 175, 54);
    addTextBoxParameterEditor (Parameter::PROCESSOR_SCOPE, "min_interval", 175, 79);

    countsLabel = std::make_unique<Label> ("Counts", "");
    countsLabel->setBounds (173, 106, 160, 18);
    countsLabel->setFont (FontOptions (11.0f));
    countsLabel->setColour (Label::textColourId, Colours::grey);
    addAndMakeVisible (countsLabel.get());

    latencyLabel = std::make_unique<Label> ("Latency", "");
    latencyLabel->setBounds (173, 122, 160, 18);
    latencyLabel->setFont (FontOptions (11.0f));
    latencyLabel->setColour (Label::textColourId, Colours::grey);
    addAndMakeVisible (latencyLabel.get());

    startTimer (250);
    refresh();
}

void LaserHatOutputEditor::buttonClicked (Button* button)
{
    auto* processor = (LaserHatOutput*) getProcessor();

    if (button == connectButton.get())
    {
        if (processor->isConnectedToBroker())
            processor->disconnectFromBroker();
        else
            processor->connectToBroker();

        refresh();
    }
    else if (button == pingButton.get())
    {
        // PING, not a trigger: the broker replies PONG without raising the
        // MCU's pin, so this proves the whole path without firing the laser.
        if (processor->getClient().ping())
            CoreServices::sendStatusMessage ("LaserHAT: ping sent");
        else
            CoreServices::sendStatusMessage ("LaserHAT: ping failed to send");
    }
}

void LaserHatOutputEditor::timerCallback()
{
    refresh();
}

void LaserHatOutputEditor::refresh()
{
    auto* processor = (LaserHatOutput*) getProcessor();
    const LhtrClient& client = processor->getClient();

    if (! processor->isConnectedToBroker())
    {
        connectButton->setLabel ("OPEN");
        pingButton->setVisible (false);
        statusLabel->setText (processor->getConnectionStatus(), dontSendNotification);
        statusLabel->setColour (Label::textColourId, Colours::grey);
        countsLabel->setText ("", dontSendNotification);
        latencyLabel->setText ("", dontSendNotification);
        return;
    }

    connectButton->setLabel ("CLOSE");
    pingButton->setVisible (true);

    // An open UDP socket says nothing about the far end, so the status line
    // distinguishes "sent into the void" from "something answered".
    if (client.hasHeardFromBroker())
    {
        statusLabel->setText ("broker: " + client.getLastStatus(), dontSendNotification);
        statusLabel->setColour (Label::textColourId, Colours::lightgreen);
    }
    else
    {
        statusLabel->setText ("open, no reply yet", dontSendNotification);
        statusLabel->setColour (Label::textColourId, Colours::orange);
    }

    String counts = String (client.getSent()) + " sent / " + String (client.getAcked()) + " ack";
    if (processor->getSuppressedCount() > 0)
        counts += " / " + String (processor->getSuppressedCount()) + " held";
    countsLabel->setText (counts, dontSendNotification);

    if (client.getLastRttUs() > 0)
    {
        String latency = "rtt " + String (client.getLastRttUs() / 1000.0f, 2) + " ms";
        if (client.getLastPiUs() > 0)
            latency += ", pi " + String (client.getLastPiUs()) + " us";
        latencyLabel->setText (latency, dontSendNotification);
    }
    else
    {
        latencyLabel->setText ("", dontSendNotification);
    }
}
