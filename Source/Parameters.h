// COHERENCE - parameter definitions, ranges, display text and the mapping onto the DSP settings.

#pragma once

#include "dsp/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace coherence
{
namespace id
{
// EQ bands: prefixes "lf", "lmf", "hmf", "hf"
inline juce::String bandFreq (const juce::String& b) { return b + "_freq"; }
inline juce::String bandGain (const juce::String& b) { return b + "_gain"; }
inline juce::String bandQ (const juce::String& b) { return b + "_q"; }
inline juce::String bandType (const juce::String& b) { return b + "_type"; }
inline juce::String sideBand (const juce::String& b) { return "side_" + b; }   // the Side channel's band, in M/S

inline const juce::StringArray bands { "lf", "lmf", "hmf", "hf" };

constexpr const char* eqOn = "eq_on";
constexpr const char* dynOn = "dyn_on";
constexpr const char* threshold = "comp_threshold";
constexpr const char* ratio = "comp_ratio";
constexpr const char* attack = "comp_attack";
constexpr const char* release = "comp_release";
constexpr const char* autoRelease = "comp_auto_release";
constexpr const char* knee = "comp_knee";
constexpr const char* makeup = "comp_makeup";
constexpr const char* autoMakeup = "comp_auto_makeup";
constexpr const char* eqOutput = "eq_output";
constexpr const char* eqMs = "eq_ms";            // Stereo or M/S
constexpr const char* eqChannel = "eq_channel";  // Mid or Side: which channel the EQ controls show
constexpr const char* focusPrefix = "focus_";     // the controls a surface uses to follow the selector (EqFocus.h)
constexpr const char* mix = "comp_mix";
constexpr const char* scHpf = "comp_sc_hpf";
constexpr const char* order = "order";
constexpr const char* input = "input";
constexpr const char* mid = "mid";
constexpr const char* side = "side";
constexpr const char* msLink = "ms_link";
constexpr const char* balance = "balance";
constexpr const char* mono = "mono";
constexpr const char* output = "output";
constexpr const char* bypass = "bypass";
constexpr const char* eqMode = "eq_mode";     // 0 = COHERENCE (zero latency), 1 = WEISS (2x, 15 samples latency)
constexpr const char* grMeter = "gr_meter";   // read-only output for control surfaces (SSL UC1 via 360 Link)
constexpr const char* abSelect = "ab_select"; // the A/B switch for control surfaces; holds no setting (DESIGN.md section 19)
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Parameters added since the first installed builds. They go after the meter, so that no existing index moves.
void addAppendedParameters (juce::AudioProcessorValueTreeState&);

// Saved with the set and part of A/B: everything except the meter and the focus parameters.
bool isSavedParameter (const juce::RangedAudioParameter&) noexcept;

// Fader law: normalised position <-> dB, shared by the parameter and the fader scale.
struct FaderLaw
{
    static constexpr int numPoints = 9;
    static constexpr float pos[numPoints] = { 0.0f, 0.12f, 0.24f, 0.38f, 0.52f, 0.62f, 0.74f, 0.87f, 1.0f };
    static constexpr float db[numPoints] = { -60.0f, -40.0f, -30.0f, -20.0f, -10.0f, -5.0f, 0.0f, 6.0f, 12.0f };
    static float toDb (float p);
    static float toPos (float d);
};

//==============================================================================
// Gain stepping at the parameter level (DESIGN.md section 11).
//
// Every dB control follows the gain-stepping preference whatever moves it. The stored value is never changed (it
// keeps 0.01 dB, exactly what the host or controller sent), so nothing is written back to the host: no feedback
// loop, no automation written, and a controller always reads back its own value. What is quantised is the value
// that is played and displayed:
//   - a value set from the plug-in's own GUI is played as set (drag and wheel already land on the step grid;
//     Shift fine adjustment and typed values are exact by intent);
//   - a value set from outside (automation, UC1/UF1 via 360 Link, any controller or host) is played and displayed
//     on the step grid of the current mode. A host passing the plug-in's own edits back (VST3 does, a block or more
//     later) is recognised as such.
// Which of the two applies is a per-parameter flag, saved with the set and carried by A/B, so a recalled set plays
// exactly as it did when it was saved. Sets from before v0.2.4 have no flag and play exactly as stored.

enum class WriteSource { host, gui, internal };

// Marks parameter changes made on this thread while it exists. The default (no scope) is the host.
// gui: the plug-in's own controls. internal: recall, A/B and undo (the flags are restored separately).
class ScopedWriteSource
{
public:
    explicit ScopedWriteSource (WriteSource);
    ~ScopedWriteSource();
    static WriteSource current() noexcept;

private:
    WriteSource previous;
    JUCE_DECLARE_NON_COPYABLE (ScopedWriteSource)
};

// v on the 0.01 dB grid, rounded to the step (half away from zero). Values already on the step grid are returned
// unchanged, bit for bit. A step of 0.01 dB (Precision) changes nothing.
float snapToGainStep (float v, float stepDb) noexcept;

class SteppedGainParameter final : public juce::AudioParameterFloat
{
public:
    SteppedGainParameter (const juce::String& pid, const juce::String& paramName, juce::NormalisableRange<float> valueRange,
                          float def, std::function<juce::String (float)> text, const juce::String& unitLabel);

    // The value that is played and displayed for a stored value.
    float effective (float stored) const noexcept;

    // True when the current value came from outside the plug-in (played on the step grid).
    bool isExternal() const noexcept { return external.load (std::memory_order_relaxed); }
    void setExternal (bool e) noexcept { external.store (e, std::memory_order_relaxed); }

private:
    void valueChanged (float newValue) override;
    juce::String getText (float normalised, int maximumLength) const override;
    bool wroteRecently (float v) const noexcept;

    std::function<juce::String (float)> toText;
    std::atomic<bool> external { false };
    std::atomic<float> last;
    // The last values the plug-in itself wrote (GUI, A/B, undo). A VST3 host passes every edit back to the
    // processor in a later audio block, so during a drag those echoes lag behind the GUI; they must not be taken
    // for a controller.
    static constexpr int recentSize = 64;
    std::array<std::atomic<float>, recentSize> recent;
    std::atomic<int> recentHead { 0 };
};

//==============================================================================
// Gain-reduction meter for control surfaces (DESIGN.md section 12). A read-only VST3 parameter (an AU meter
// parameter), not automatable, not saved with the set and not part of A/B. Its value follows SSL's convention for
// 360 Link: 0 = no reduction, 1 = full scale (silence), value = 1 - 10^(GR dB / 20); Link shows 20 log10 (1 - value).
// The value is the compressor's actual gain reduction: the deepest reduction applied in each audio block, with no
// meter ballistics added (v0.3.0; the compressor's own release is the meter's release).
std::unique_ptr<juce::AudioParameterFloat> createGainReductionMeter();

// A control surface can only reach the plug-in through a parameter, so this is how it switches A and B. It shows which
// slot is selected and holds no setting: it isn't saved, isn't in A/B, isn't automatable, and the audio never reads it.
std::unique_ptr<juce::AudioParameterChoice> createAbSelectParameter();
float gainReductionToMeterValue (float grDb) noexcept;   // grDb <= 0
float meterValueToGainReduction (float v) noexcept;
bool isMeterParameter (const juce::AudioProcessorParameter&) noexcept;

class GainReductionBallistics
{
public:
    // grDb: the deepest reduction applied during the block (<= 0). Returns the meter reading in dB: the actual
    // reduction, without added ballistics (before v0.3.0 the release was held to 30 dB/s like the panel meter).
    float process (float grDb, float /*seconds*/) noexcept
    {
        // Below 0.001 dB the reading is exactly 0, so the meter returns to rest (the release approaches 0 asymptotically).
        reading = grDb > -1.0e-3f ? 0.0f : grDb;
        return reading;
    }
    void reset() noexcept { reading = 0.0f; }

private:
    float reading = 0.0f;
};

// Fast, lock-free access to the parameter values from the audio thread.
class ParameterReader
{
public:
    explicit ParameterReader (juce::AudioProcessorValueTreeState&);
    dsp::Settings read() const;
    void attachAppended (juce::AudioProcessorValueTreeState&);   // after addAppendedParameters()

private:
    struct Band
    {
        std::atomic<float>* freq;
        std::atomic<float>* gain;
        std::atomic<float>* q;
        std::atomic<float>* type;   // null for the mid bands
        const SteppedGainParameter* gainParam;
    };
    std::array<Band, 4> bands {};
    std::atomic<float> *eqOn, *dynOn, *threshold, *ratio, *attack, *release, *autoRelease, *knee, *makeup,
        *autoMakeup, *mix, *scHpf, *order, *input, *mid, *side, *balance, *mono, *output, *bypass, *eqMode;
    const SteppedGainParameter *thresholdParam, *makeupParam, *inputParam, *midParam, *sideParam, *outputParam;
    std::atomic<float>* eqOutput = nullptr;
    const SteppedGainParameter* eqOutputParam = nullptr;
    std::atomic<float>* eqMs = nullptr;
    std::array<Band, 4> sideBands {};
};

// Short descriptions shown in the status line when hovering a control.
juce::String describe (const juce::String& paramId);
} // namespace coherence
