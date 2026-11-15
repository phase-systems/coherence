// COHERENCE - the EQ controls a control surface uses (DESIGN.md section 18).
//
// A surface binds each of its knobs to one fixed parameter, so it can't follow the Mid/Side selector by itself. It
// binds to these twelve "focus" parameters instead (gain, frequency and Q of each band). Each one stands for the same
// control on whichever channel is selected: writes from outside go to that channel's parameter, and sync() keeps its
// value and readout on that channel. They hold no setting of their own: they aren't saved, aren't in A/B, aren't
// automatable, and the audio never reads them.

#pragma once

#include "Parameters.h"

#include <juce_events/juce_events.h>

namespace coherence
{
class EqFocus final : private juce::AudioProcessorParameter::Listener, private juce::Timer
{
public:
    // Adds the parameters to the processor, after every other one, so that no existing index moves.
    EqFocus (juce::AudioProcessor&, juce::AudioProcessorValueTreeState&);
    ~EqFocus() override;

    // Brings each focus parameter to the selected channel's value, telling the host, so that the surface's display
    // follows the panel, automation and the selector. Message thread: 30 times a second, and from the tests.
    void sync();

private:
    static constexpr int numEntries = 12;
    struct Entry
    {
        juce::RangedAudioParameter* focus = nullptr;
        juce::RangedAudioParameter* main = nullptr;
        juce::RangedAudioParameter* side = nullptr;
        // sync()'s own recent writes, and when. A VST3 host passes them back in a later block; they must not be taken
        // for a surface and sent to the channel, which may have moved on by then.
        static constexpr int recentSize = 16;
        std::array<std::atomic<float>, recentSize> recent;
        std::atomic<int> recentHead { 0 };
        std::atomic<juce::uint32> lastSyncMs { 0 }, lastOutsideMs { 0 };
    };

    juce::RangedAudioParameter* selected (const Entry&) const noexcept;
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}
    void timerCallback() override { sync(); }

    std::array<Entry, numEntries> entries;
    std::atomic<float>* msOn = nullptr;
    std::atomic<float>* sideSelected = nullptr;
    int firstIndex = 0;
};
} // namespace coherence
