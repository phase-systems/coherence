#pragma once

#include "Parameters.h"
#include "EqFocus.h"
#include "UserSettings.h"
#include "dsp/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace coherence
{
class CoherenceProcessor final : public juce::AudioProcessor,
                                 private juce::AudioProcessorParameter::Listener,
                                 private juce::AsyncUpdater,
                                 private juce::ChangeListener
{
public:
    CoherenceProcessor();
    ~CoherenceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Worst-case ring-out: a Q 10 bell at 16 Hz decays to -100 dB in about 1.6 s. Hosts that suspend idle
    // plug-ins (Logic) use this so that tails are not cut off. Ableton processes continuously.
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    // A/B compare: two complete settings snapshots. The live parameters always belong to the selected slot.
    int getAbSlot() const { return abSlot.load(); }
    void selectAbSlot (int slot);
    void toggleAbSlot();
    void copyCurrentToOtherSlot();

    // The A/B switch for control surfaces (createAbSelectParameter()). A write from outside selects that slot, and the
    // parameter follows every switch, so a surface's light and the panel stay in step.
    juce::AudioParameterChoice& abSelect() { return *abParam; }
    // Applies a pending switch and brings the parameter up to date. The message thread does this by itself; for the tests.
    void syncAbSurface() { handleUpdateNowIfNeeded(); }

    float getUiScale() const;
    void setUiScale (float);

    // The gain-reduction meter parameter for control surfaces (read-only; see createGainReductionMeter()).
    juce::AudioParameterFloat& gainReductionMeter() { return *grMeter; }

    juce::AudioProcessorValueTreeState& state() { return apvts; }
    EqFocus& focus() { return *eqFocus; }
    juce::UndoManager& undo() { return undoManager; }
    dsp::MeterData& meters() { return meterData; }
    double currentSampleRate() const { return getSampleRate(); }

    // Undo/redo from the UI. Values come back exactly as they were; the gain-stepping flags are left as they are.
    void undoStep (bool redo);

    // The most entries one undo step may hold before the history starts again (changeListenerCallback()). A panel
    // gesture is one entry, or about 100 a second while two linked controls move together, so only a host writing
    // values for a long time gets here: about 24 s of automation on two parameters.
    static constexpr int maxUndoActionsPerStep = 2048;

private:
    // Keeps the user preferences loaded while any instance exists, so the gain step reaches the DSP even with no
    // editor open (e.g. a session with only UC1 control).
    juce::SharedResourcePointer<UserSettings> userSettings;
    juce::UndoManager undoManager { 30000, 30 };
    juce::AudioProcessorValueTreeState apvts;
    ParameterReader reader;
    std::unique_ptr<EqFocus> eqFocus;   // after the parameters it refers to
    dsp::Engine engine;
    dsp::MeterData meterData;
    juce::AudioParameterFloat* grMeter = nullptr;   // owned by the processor; outside the APVTS (no undo, no state)
    GainReductionBallistics grBallistics;          // audio thread
    double preparedRate = 0.0;
    bool reportedWeiss = false;                    // the EQ mode whose latency was last reported to the host

    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;
    void syncAbParameter();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // the undo history changed (message thread)

    juce::AudioParameterChoice* abParam = nullptr;   // owned by the processor; outside the APVTS (no undo, no state)
    std::atomic<int> pendingAbSlot { -1 };           // a switch from outside, waiting for the message thread
    // syncAbParameter()'s recent writes (one bit per slot), and when. A VST3 host passes them back in a later block; they
    // must not be taken for a surface and switch the slot, which may have moved on by then.
    std::atomic<int> abWrittenMask { 0 };
    std::atomic<juce::uint32> lastAbWriteMs { 0 };

    std::atomic<int> abSlot { 0 };
    std::atomic<float> uiScale { -1.0f };  // window scale saved with the set; -1 = not chosen yet (the editor uses the
                                           // user's default). Kept outside the ValueTree so saving never touches it.
    juce::ValueTree otherSlot;   // the parameter state of the slot that isn't selected
    // True until the other slot has settings of its own. The first switch then makes it a copy of this one, so that a
    // press on its own changes nothing but the slot (DESIGN.md section 19). Saved with the set.
    bool otherSlotFresh = true;
    // Guards otherSlot and otherSlotFresh: hosts may save state from a background thread while A/B is switched on the UI thread.
    juce::CriticalSection slotLock;

    // One PARAM child per parameter ("id", "value", and "ext" = 1 for dB values that came from outside, which
    // play on the step grid). Built from the parameters' atomics, so it is safe on any thread.
    juce::ValueTree parameterSnapshot() const;
    void applyExternalFlags (const juce::ValueTree&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoherenceProcessor)
};
} // namespace coherence
