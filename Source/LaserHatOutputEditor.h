// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

#ifndef LASERHAT_OUTPUT_EDITOR_H_INCLUDED
#define LASERHAT_OUTPUT_EDITOR_H_INCLUDED

#include "LaserHatOutput.h"
#include <EditorHeaders.h>

/**
    User interface for LaserHatOutput.

    @see LaserHatOutput
*/
class LaserHatOutputEditor : public GenericEditor,
                             public Timer,
                             public Button::Listener
{
public:
    LaserHatOutputEditor (GenericProcessor* parentNode);
    ~LaserHatOutputEditor() override {}

    void buttonClicked (Button* button) override;

private:
    void timerCallback() override;

    /** Repaint the connect button, status line and counters. */
    void refresh();

    std::unique_ptr<UtilityButton> connectButton;
    std::unique_ptr<UtilityButton> pingButton;
    std::unique_ptr<Label> statusLabel;
    std::unique_ptr<Label> countsLabel;
    std::unique_ptr<Label> latencyLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaserHatOutputEditor)
};

#endif // LASERHAT_OUTPUT_EDITOR_H_INCLUDED
