#pragma once

#include "PluginProcessor.h"
#include "UserSettings.h"
#include "gui/Controls.h"
#include "gui/Meters.h"

namespace coherence
{
class CoherenceEditor final : public juce::AudioProcessorEditor,
                              public gui::HoverSink,
                              private juce::Timer,
                              private juce::ChangeListener
{
public:
    explicit CoherenceEditor (CoherenceProcessor&);
    ~CoherenceEditor() override;

    // 100 % is 1.25 x the panel's coordinates (720 x 666, the proportions of the 4K Series panels).
    static constexpr int panelWidth = 720, panelHeight = 666;
    static constexpr float panelToBase = 1.25f;
    static constexpr int baseWidth = 900, baseHeight = 832;

    void resized() override;
    void hoverChanged (juce::RangedAudioParameter*, const juce::String& fallback) override;
    void hoverValue (std::function<juce::String()>) override;

    // Window size: the panel scales as a whole (one transform), aspect ratio locked, 60-200 % of the base size.
    static constexpr float minScale = 0.6f, maxScale = 2.0f;
    struct SizePreset { const char* name; float scale; };
    static constexpr SizePreset sizePresets[] = { { "Small", 0.8f }, { "Medium", 1.0f }, { "Large", 1.2f }, { "XL", 1.4f } };

    // For snapshots and tests.
    void setScale (float);
    float getScale() const { return scale; }
    void tickMetersForPreview (int frames);
    gui::Knob* findKnob (const juce::String& parameterID) const;
    gui::Fader* findFader() const;
    gui::ConsoleButton* findAbButton (int slot) const;
    gui::Segmented* findEqChannelSelector() const;
    void applyGainStepping();          // re-reads the user setting and applies it to every dB control
    void setEqMode (int mode);         // SETUP > EQ mode: 0 = COHERENCE, 1 = WEISS (a parameter, saved with the set)

private:
    class Content;
    class Panel;
    class HeaderDisplay;
    class StatusDisplay;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void applyTheme();         // the user's theme: a repaint, and nothing else changes
    void updateStatus();
    void updateBandStates();
    void updateEqChannel();   // shows the main or the Side controls
    void showSetupMenu();
    void requestScale (float);         // user choice: resize, and remember it for this instance and new ones
    juce::RangedAudioParameter& p (const juce::String& pid) { return *proc.state().getParameter (pid); }

    CoherenceProcessor& proc;
    std::unique_ptr<juce::LookAndFeel> lookAndFeel;   // first, so it outlives every child that uses it
    juce::SharedResourcePointer<UserSettings> settings;
    std::unique_ptr<Content> content;
    juce::ComponentBoundsConstrainer constrainer;
    float scale = 1.0f;
    juce::RangedAudioParameter* hoverParam = nullptr;
    juce::String hoverFallback;
    std::function<juce::String()> hoverValueSource;
    bool lastBypass = false;
    int lastEqMode = -1;
    int lastTypes = -1, lastEqChannel = -1;
    int lastLatency = -1;
    double lastRate = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoherenceEditor)
};
} // namespace coherence
