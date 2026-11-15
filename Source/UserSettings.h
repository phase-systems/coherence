// COHERENCE - user preferences that belong to the user, not to a session.
//
// Stored in one small file shared by every instance and every session:
//   macOS: ~/Library/Application Support/Phase Systems/COHERENCE.settings
//   Linux: ~/.config/Phase Systems/COHERENCE.settings (tests)
// They never touch the plug-in's state, parameters or audio. Message thread only. Open editors are notified of
// changes (juce::ChangeBroadcaster), so all instances follow a change made in one of them.

#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace coherence
{
// The gain step of every dB control. In the GUI, drag and wheel move by the step; Shift (or Cmd) gives 0.01 dB and
// typed values are exact. Values from the host (automation, UC1/UF1 via 360 Link, other controllers) are played
// and displayed on the step grid, without changing the stored value (DESIGN.md section 11).
enum class GainStepping
{
    fine = 0,       // 0.1 dB (default; the v0.2 behaviour)
    standard = 1,   // 0.5 dB
    precision = 2   // 0.01 dB
};

float gainStepDb (GainStepping);
juce::String gainSteppingLabel (GainStepping);   // e.g. "Fine (0.1 dB)"

// The current step in dB, for the audio thread and the parameter display (lock-free). Set from the settings file
// when the first UserSettings is created, and on every change; every instance in the process follows it.
float currentGainStepDb() noexcept;

class UserSettings final : public juce::ChangeBroadcaster
{
public:
    UserSettings();
    ~UserSettings() override;

    GainStepping gainStepping() const;
    void setGainStepping (GainStepping);

    // The window size new instances open at (the last size chosen in any instance). Sets keep their own.
    float defaultScale() const;
    void setDefaultScale (float);

    // The panel's theme (theme::Palette::id). It belongs to the user: never saved with a set or preset.
    juce::String themeId() const;
    void setThemeId (const juce::String&);

    // Tests: use this file instead of the user's, from the next UserSettings created.
    static void setFileForTesting (const juce::File&);
    juce::File file() const;

private:
    std::unique_ptr<juce::PropertiesFile> props;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UserSettings)
};
} // namespace coherence
