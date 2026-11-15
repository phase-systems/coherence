#include "Parameters.h"
#include "UserSettings.h"

namespace coherence
{
namespace
{
using Range = juce::NormalisableRange<float>;
using Attrs = juce::AudioParameterFloatAttributes;

// Round to a number of significant digits (the parameter's display resolution).
float snapSignificant (float v, int digits)
{
    if (v <= 0.0f)
        return v;
    const double e = std::floor (std::log10 ((double) v));
    const double scale = std::pow (10.0, e - digits + 1);
    return (float) (std::round ((double) v / scale) * scale);
}

// Logarithmic range. `snap` rounds every value to exactly what the readout shows, so the DSP always
// runs at the displayed value.
Range logRange (float lo, float hi, std::function<float (float)> snap)
{
    return Range (lo, hi,
                  [] (float a, float b, float n) { return a * std::pow (b / a, n); },
                  [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                  [snap] (float a, float b, float v) { return juce::jlimit (a, b, snap (juce::jlimit (a, b, v))); });
}

juce::String trimNumber (double v, int maxDecimals, int minDecimals)
{
    auto s = juce::String (v, maxDecimals);
    if (s.containsChar ('.'))
    {
        while (s.endsWithChar ('0') && s.fromLastOccurrenceOf (".", false, false).length() > minDecimals)
            s = s.dropLastCharacters (1);
        if (s.endsWithChar ('.'))
            s = s.dropLastCharacters (1);
    }
    return s;
}

juce::String dbText (float v, bool unit = true)
{
    const double r = std::round ((double) v * 100.0) / 100.0;
    juce::String s = std::abs (r) < 0.005 ? "0.0" : (r > 0 ? "+" : "-") + trimNumber (std::abs (r), 2, 1);
    s = s.replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92"));   // true minus sign
    return unit ? s + " dB" : s;
}

// EQ frequencies: 4 significant digits, shown in full (what you read is exactly what runs).
float snapFreq4 (float v) { return snapSignificant (v, 4); }
juce::String hzText4 (float v)
{
    if (v >= 10000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
    if (v >= 1000.0f)  return juce::String (v / 1000.0f, 3) + " kHz";
    if (v >= 100.0f)   return juce::String (v, 1) + " Hz";
    return juce::String (v, 2) + " Hz";
}

// Sidechain filter: 3 significant digits are plenty.
float snapFreq3 (float v) { return snapSignificant (v, 3); }
juce::String hzText3 (float v)
{
    if (v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
    if (v >= 100.0f)  return juce::String (juce::roundToInt (v)) + " Hz";
    return juce::String (v, 1) + " Hz";
}

// Q: 3 significant digits (0.707, 1.41, 10.0).
float snapQ (float v) { return snapSignificant (v, 3); }
juce::String qText (float v)
{
    if (v < 0.9995f) return juce::String (v, 3);
    if (v < 9.995f)  return juce::String (v, 2);
    return juce::String (v, 1);
}

// Times in ms: 1 us below 1 ms, 0.1 ms below 10 ms, 1 ms below 1 s, 10 ms above.
float snapMs (float v)
{
    if (v < 1.0f)    return std::round (v * 1000.0f) / 1000.0f;
    if (v < 10.0f)   return std::round (v * 10.0f) / 10.0f;
    if (v < 1000.0f) return std::round (v);
    return std::round (v / 10.0f) * 10.0f;
}
juce::String msText (float v)
{
    if (v < 1.0f)     return juce::String (juce::roundToInt (v * 1000.0f)) + juce::String::fromUTF8 (" \xc2\xb5s");
    if (v < 10.0f)    return juce::String (v, 1) + " ms";
    if (v < 1000.0f)  return juce::String (juce::roundToInt (v)) + " ms";
    return juce::String (v / 1000.0f, 2) + " s";
}

float snapRatio (float v)
{
    if (v >= 999.0f) return 1000.0f;
    if (v >= 10.0f)  return std::round (v);
    return std::round (v * 10.0f) / 10.0f;
}
juce::String ratioText (float v)
{
    if (v >= 999.0f) return juce::String::fromUTF8 ("\xe2\x88\x9e : 1");
    if (v >= 10.0f)  return juce::String (juce::roundToInt (v)) + " : 1";
    return juce::String (v, 1) + " : 1";
}

// Accepts "2.5k", "2500", "-3.5 dB", "40 ms", "1.2 s", "inf".
float parseNumber (const juce::String& text)
{
    // Normalise the true minus sign and the infinity sign on the raw UTF-8 bytes first.
    std::string raw = text.trim().toStdString();
    const auto swap = [&raw] (const std::string& from, const std::string& to)
    {
        for (size_t i = raw.find (from); i != std::string::npos; i = raw.find (from, i + to.size()))
            raw.replace (i, from.size(), to);
    };
    swap ("\xe2\x88\x92", "-");
    swap ("\xe2\x88\x9e", "inf");
    auto t = juce::String (raw).toLowerCase();
    if (t.contains ("inf"))
        return t.startsWith ("-") ? -1.0e9f : 1.0e9f;
    if (t.containsChar (':'))
        t = t.upToFirstOccurrenceOf (":", false, false);
    if (t == "c" || t == "off")
        return 0.0f;
    if (t.startsWith ("l") && ! t.contains ("hz"))   // balance "L 20"
        return -t.retainCharacters ("0123456789.").getFloatValue();
    float mult = 1.0f;
    if (t.contains ("khz") || t.endsWith ("k"))
        mult = 1000.0f;
    else if (t.endsWith (" s") || (t.endsWith ("s") && ! t.endsWith ("ms") && ! t.contains (juce::String::fromUTF8 ("\xc2\xb5"))))
        mult = 1000.0f;   // seconds -> ms
    else if (t.contains (juce::String::fromUTF8 ("\xc2\xb5")) || t.endsWith ("us"))
        mult = 0.001f;    // microseconds -> ms
    return t.retainCharacters ("0123456789.-+").getFloatValue() * mult;
}

std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& pid, const juce::String& name, Range range,
                                                       float def, std::function<juce::String (float)> toText,
                                                       const juce::String& label = {})
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid, 1 }, name, range, def,
        Attrs().withLabel (label)
            .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); }));
}

std::unique_ptr<SteppedGainParameter> gainParam (const juce::String& pid, const juce::String& name, Range range, float def,
                                                 std::function<juce::String (float)> toText)
{
    return std::make_unique<SteppedGainParameter> (pid, name, range, def, std::move (toText), "dB");
}

std::unique_ptr<juce::AudioParameterBool> boolParam (const juce::String& pid, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid, 1 }, name, def);
}

struct BandDef { const char* key; const char* name; float lo, hi, def, q; };
const BandDef bandDefs[] = { { "lf", "LF", 16.0f, 1000.0f, 80.0f, 0.707f },
                             { "lmf", "LMF", 40.0f, 4000.0f, 320.0f, 1.0f },
                             { "hmf", "HMF", 300.0f, 16000.0f, 2500.0f, 1.0f },
                             { "hf", "HF", 1000.0f, 20000.0f, 10000.0f, 0.707f } };
} // namespace

//==============================================================================
float FaderLaw::toDb (float p)
{
    p = juce::jlimit (0.0f, 1.0f, p);
    for (int i = 1; i < numPoints; ++i)
        if (p <= pos[i])
            return juce::jmap (p, pos[i - 1], pos[i], db[i - 1], db[i]);
    return db[numPoints - 1];
}

float FaderLaw::toPos (float d)
{
    d = juce::jlimit (db[0], db[numPoints - 1], d);
    for (int i = 1; i < numPoints; ++i)
        if (d <= db[i])
            return juce::jmap (d, db[i - 1], db[i], pos[i - 1], pos[i]);
    return 1.0f;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Input
    layout.add (gainParam (id::input, "Input", Range (-24.0f, 24.0f, 0.01f), 0.0f, [] (float v) { return dbText (v); }));

    // EQ
    layout.add (boolParam (id::eqOn, "EQ In", true));
    for (const auto& d : bandDefs)
    {
        const juce::String b (d.key), n (d.name);
        layout.add (gainParam (id::bandGain (b), n + " Gain", Range (-15.0f, 15.0f, 0.01f), 0.0f, [] (float v) { return dbText (v); }));
        layout.add (floatParam (id::bandFreq (b), n + " Freq", logRange (d.lo, d.hi, snapFreq4), d.def, hzText4, "Hz"));
        layout.add (floatParam (id::bandQ (b), n + " Q", logRange (0.3f, 10.0f, snapQ), d.q, qText));
        if (b == "lf" || b == "hf")
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::bandType (b), 1 }, n + " Type",
                                                                      juce::StringArray { "Bell", "Shelf", "Cut" }, 1));
    }

    // Dynamics
    layout.add (boolParam (id::dynOn, "Dyn In", false));
    layout.add (gainParam (id::threshold, "Threshold", Range (-60.0f, 0.0f, 0.01f), -18.0f, [] (float v) { return dbText (v); }));
    {
        // Ratio: the knob moves the slope (1 - 1/ratio) with a gentle curve, so 1.5:1 .. 6:1 has most of the travel.
        Range r (1.0f, 1000.0f,
                 [] (float, float, float n) { const float s = std::pow (juce::jlimit (0.0f, 1.0f, n), 0.8f); return s >= 0.999f ? 1000.0f : 1.0f / (1.0f - s); },
                 [] (float, float, float v) { const float s = v >= 1000.0f ? 1.0f : 1.0f - 1.0f / juce::jmax (1.0f, v); return std::pow (s, 1.0f / 0.8f); },
                 [] (float, float, float v) { return snapRatio (juce::jlimit (1.0f, 1000.0f, v)); });
        layout.add (floatParam (id::ratio, "Ratio", r, 4.0f, ratioText));
    }
    layout.add (floatParam (id::attack, "Attack", logRange (0.05f, 150.0f, snapMs), 10.0f, msText, "ms"));
    layout.add (floatParam (id::release, "Release", logRange (5.0f, 3000.0f, snapMs), 150.0f, msText, "ms"));
    layout.add (boolParam (id::autoRelease, "Auto Release", false));
    {
        // Knee: the stored value stays 0..1 (sessions and automation unchanged); it is shown as the width in dB,
        // 72 * v^2.4 (0.4 = 8 dB as before, 1 = 72 dB as Pro-C 3). Typed values are in dB.
        auto knee = std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::knee, 1 }, "Knee", Range (0.0f, 1.0f, 0.01f), 0.4f,
            Attrs().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { const double k = dsp::kneeWidthDb (v); return juce::String (k, k < 0.0995 ? 3 : (k < 0.995 ? 2 : (k < 9.95 ? 1 : 0))) + " dB"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return (float) std::pow (juce::jlimit (0.0, 1.0, (double) parseNumber (t) / dsp::kneeMaxDb), 1.0 / 2.4); }));
        layout.add (std::move (knee));
    }
    layout.add (gainParam (id::makeup, "Makeup", Range (-24.0f, 24.0f, 0.01f), 0.0f, [] (float v) { return dbText (v); }));
    layout.add (boolParam (id::autoMakeup, "Auto Makeup", true));
    layout.add (floatParam (id::mix, "Mix", Range (0.0f, 100.0f, 0.1f), 100.0f,
                            [] (float v) { return juce::String (v, v < 99.95f && v > 0.05f && std::abs (v - std::round (v)) > 0.05f ? 1 : 0) + " %"; }, "%"));
    layout.add (floatParam (id::scHpf, "SC HPF", logRange (20.0f, 500.0f, [] (float v) { const float s = snapFreq3 (v); return s <= 20.5f ? 20.0f : s; }), 20.0f,
                            [] (float v) { return v <= 20.5f ? juce::String ("OFF") : hzText3 (v); }, "Hz"));

    // Order
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::order, 1 }, "Order",
                                                              juce::StringArray { "EQ > DYN", "DYN > EQ" }, 0));

    // Architecture
    layout.add (gainParam (id::mid, "Mid Level", Range (-12.0f, 12.0f, 0.01f), 0.0f, [] (float v) { return dbText (v); }));
    layout.add (gainParam (id::side, "Side Level", Range (-12.0f, 12.0f, 0.01f), 0.0f, [] (float v) { return dbText (v); }));
    layout.add (boolParam (id::msLink, "M/S Link", false));
    layout.add (floatParam (id::balance, "Balance", Range (-100.0f, 100.0f, 0.1f), 0.0f,
                            [] (float v)
                            {
                                if (std::abs (v) < 0.05f) return juce::String ("C");
                                return juce::String (v < 0 ? "L " : "R ") + trimNumber (std::abs (v), 1, 0);
                            }));
    layout.add (boolParam (id::mono, "Mono", false));

    // Output
    {
        Range r (FaderLaw::db[0], FaderLaw::db[FaderLaw::numPoints - 1],
                 [] (float, float, float n) { return FaderLaw::toDb (n); },
                 [] (float, float, float v) { return FaderLaw::toPos (v); },
                 [] (float a, float b, float v) { return juce::jlimit (a, b, std::round (v * 100.0f) / 100.0f); });
        layout.add (gainParam (id::output, "Output", r, 0.0f,
                               [] (float v) { return v <= -59.995f ? juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e dB") : dbText (v); }));
    }
    layout.add (boolParam (id::bypass, "Bypass", false));

    // EQ mode (v0.2.5). Not automatable: it changes the latency. Last in the list, so nothing else moves.
    // New instances start in Weiss mode (v0.6.1). A saved set keeps its own mode, see setStateInformation().
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::eqMode, 1 }, "EQ Mode", juce::StringArray { "COHERENCE", "WEISS" }, 1,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    return layout;
}

//==============================================================================
namespace
{
thread_local WriteSource writeSource = WriteSource::host;
}

ScopedWriteSource::ScopedWriteSource (WriteSource s) : previous (writeSource) { writeSource = s; }
ScopedWriteSource::~ScopedWriteSource() { writeSource = previous; }
WriteSource ScopedWriteSource::current() noexcept { return writeSource; }

float snapToGainStep (float v, float stepDb) noexcept
{
    const auto s = (long long) std::llround ((double) stepDb * 100.0);
    if (s <= 1)
        return v;
    const auto u = (long long) std::llround ((double) v * 100.0);
    const auto q = u >= 0 ? (u + s / 2) / s : -((-u + s / 2) / s);
    if (q * s == u)
        return v;
    return (float) ((double) (q * s) / 100.0);
}

SteppedGainParameter::SteppedGainParameter (const juce::String& pid, const juce::String& paramName, Range valueRange, float def,
                                            std::function<juce::String (float)> text, const juce::String& unitLabel)
    : juce::AudioParameterFloat (juce::ParameterID { pid, 1 }, paramName, valueRange, def,
                                 Attrs().withLabel (unitLabel)
                                     .withStringFromValueFunction ([text] (float v, int) { return text (v); })
                                     .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })),
      toText (std::move (text)),
      last (def)
{
    for (auto& r : recent)
        r.store (std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
}

bool SteppedGainParameter::wroteRecently (float v) const noexcept
{
    for (const auto& r : recent)
        if (std::abs (r.load (std::memory_order_relaxed) - v) < 0.004f)   // NaN (empty) never matches
            return true;
    return false;
}

float SteppedGainParameter::effective (float stored) const noexcept
{
    if (! isExternal())
        return stored;
    const auto& r = getNormalisableRange();
    if (stored <= r.start + 0.005f)   // the output fader's OFF position stays OFF
        return stored;
    return juce::jlimit (r.start, r.end, snapToGainStep (stored, currentGainStepDb()));
}

void SteppedGainParameter::valueChanged (float newValue)
{
    const float previous = last.exchange (newValue);
    const auto source = ScopedWriteSource::current();
    if (source != WriteSource::host)
    {
        const int i = recentHead.load (std::memory_order_relaxed);
        recent[(size_t) i].store (newValue, std::memory_order_relaxed);
        recentHead.store ((i + 1) % recentSize, std::memory_order_relaxed);
        if (source == WriteSource::gui)
            setExternal (false);
        return;
    }
    // From the host: the same value again, or one of the plug-in's own edits passed back, changes nothing.
    if (std::abs (newValue - previous) > 0.004f && ! wroteRecently (newValue))
        setExternal (true);
}

juce::String SteppedGainParameter::getText (float normalised, int maximumLength) const
{
    const auto text = toText (effective (convertFrom0to1 (normalised)));
    // JUCE's AU wrapper asks with a maximum length of 0 (no limit); truncating to it left the readout empty.
    return maximumLength > 0 ? text.substring (0, maximumLength) : text;
}

//==============================================================================
float gainReductionToMeterValue (float grDb) noexcept
{
    return juce::jlimit (0.0f, 1.0f, 1.0f - std::pow (10.0f, juce::jmin (0.0f, grDb) / 20.0f));
}

float meterValueToGainReduction (float v) noexcept
{
    return v >= 1.0f ? -200.0f : 20.0f * std::log10 (1.0f - juce::jmax (0.0f, v));
}

bool isMeterParameter (const juce::AudioProcessorParameter& p) noexcept
{
    return ((((unsigned int) p.getCategory()) & 0xffff0000) >> 16) == 2;
}

bool isSavedParameter (const juce::RangedAudioParameter& p) noexcept
{
    return ! isMeterParameter (p) && ! p.getParameterID().startsWith (id::focusPrefix) && p.getParameterID() != id::abSelect;
}

void addAppendedParameters (juce::AudioProcessorValueTreeState& apvts)
{
    using Range = juce::NormalisableRange<float>;
    apvts.createAndAddParameter (gainParam (id::eqOutput, "EQ Output", Range (-15.0f, 15.0f, 0.01f), 0.0f, [] (float v) { return dbText (v); }));
    apvts.createAndAddParameter (boolParam (id::eqMs, "EQ M/S", false));
    apvts.createAndAddParameter (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::eqChannel, 1 }, "EQ Channel",
                                                                               juce::StringArray { "Mid", "Side" }, 0));
    // The Side channel: the same bands and ranges as the main EQ. It starts flat.
    for (const auto& d : bandDefs)
    {
        const juce::String b = id::sideBand (d.key), n = juce::String ("Side ") + d.name;
        apvts.createAndAddParameter (gainParam (id::bandGain (b), n + " Gain", Range (-15.0f, 15.0f, 0.01f), 0.0f, [] (float v) { return dbText (v); }));
        apvts.createAndAddParameter (floatParam (id::bandFreq (b), n + " Freq", logRange (d.lo, d.hi, snapFreq4), d.def, hzText4, "Hz"));
        apvts.createAndAddParameter (floatParam (id::bandQ (b), n + " Q", logRange (0.3f, 10.0f, snapQ), d.q, qText));
        if (juce::String (d.key) == "lf" || juce::String (d.key) == "hf")
            apvts.createAndAddParameter (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::bandType (b), 1 }, n + " Type",
                                                                                       juce::StringArray { "Bell", "Shelf", "Cut" }, 1));
    }
}

std::unique_ptr<juce::AudioParameterChoice> createAbSelectParameter()
{
    // Meta: moving it moves other parameters, and Audio Units have to declare that (see EqFocus.cpp).
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::abSelect, 1 }, "A/B", juce::StringArray { "A", "B" }, 0,
                                                         juce::AudioParameterChoiceAttributes().withAutomatable (false).withMeta (true));
}

std::unique_ptr<juce::AudioParameterFloat> createGainReductionMeter()
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::grMeter, 1 }, "Gain Reduction", Range (0.0f, 1.0f), 0.0f,
        Attrs().withLabel ("dB")
            .withAutomatable (false)
            .withCategory (juce::AudioProcessorParameter::compressorLimiterGainReductionMeter)
            .withStringFromValueFunction ([] (float v, int) { return dbText (meterValueToGainReduction (v)); })
            .withValueFromStringFunction ([] (const juce::String& t) { return gainReductionToMeterValue (parseNumber (t)); }));
}

//==============================================================================
ParameterReader::ParameterReader (juce::AudioProcessorValueTreeState& s)
{
    auto p = [&s] (const juce::String& pid) { auto* v = s.getRawParameterValue (pid); jassert (v != nullptr); return v; };
    auto sp = [&s] (const juce::String& pid)
    { auto* q = dynamic_cast<const SteppedGainParameter*> (s.getParameter (pid)); jassert (q != nullptr); return q; };
    for (int i = 0; i < 4; ++i)
    {
        const auto& b = id::bands[i];
        bands[(size_t) i] = { p (id::bandFreq (b)), p (id::bandGain (b)), p (id::bandQ (b)),
                              (b == "lf" || b == "hf") ? p (id::bandType (b)) : nullptr, sp (id::bandGain (b)) };
    }
    eqOn = p (id::eqOn); dynOn = p (id::dynOn); threshold = p (id::threshold); ratio = p (id::ratio);
    attack = p (id::attack); release = p (id::release); autoRelease = p (id::autoRelease); knee = p (id::knee);
    makeup = p (id::makeup); autoMakeup = p (id::autoMakeup); mix = p (id::mix); scHpf = p (id::scHpf);
    order = p (id::order); input = p (id::input); mid = p (id::mid); side = p (id::side); balance = p (id::balance);
    mono = p (id::mono); output = p (id::output); bypass = p (id::bypass);
    eqMode = p (id::eqMode);
    thresholdParam = sp (id::threshold); makeupParam = sp (id::makeup); inputParam = sp (id::input);
    midParam = sp (id::mid); sideParam = sp (id::side); outputParam = sp (id::output);
}

void ParameterReader::attachAppended (juce::AudioProcessorValueTreeState& apvts)
{
    eqOutput = apvts.getRawParameterValue (id::eqOutput);
    eqOutputParam = dynamic_cast<const SteppedGainParameter*> (apvts.getParameter (id::eqOutput));
    jassert (eqOutput != nullptr && eqOutputParam != nullptr);
    eqMs = apvts.getRawParameterValue (id::eqMs);
    for (int i = 0; i < 4; ++i)
    {
        const auto b = id::sideBand (id::bands[i]);
        const bool hasType = juce::String (id::bands[i]) == "lf" || juce::String (id::bands[i]) == "hf";
        sideBands[(size_t) i] = { apvts.getRawParameterValue (id::bandFreq (b)), apvts.getRawParameterValue (id::bandGain (b)), apvts.getRawParameterValue (id::bandQ (b)),
                                  hasType ? apvts.getRawParameterValue (id::bandType (b)) : nullptr,
                                  dynamic_cast<const SteppedGainParameter*> (apvts.getParameter (id::bandGain (b))) };
    }
}

dsp::Settings ParameterReader::read() const
{
    const auto f = [] (const std::atomic<float>* a) { return (double) a->load (std::memory_order_relaxed); };
    const auto on = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed) >= 0.5f; };
    // dB controls: the played value (on the step grid when it came from outside; see SteppedGainParameter).
    const auto g = [] (const std::atomic<float>* a, const SteppedGainParameter* q)
    { return (double) q->effective (a->load (std::memory_order_relaxed)); };

    dsp::Settings s;
    s.bypass = on (bypass);
    s.inputDb = g (input, inputParam);
    s.eqOn = on (eqOn);
    s.weissEq = juce::roundToInt (eqMode->load (std::memory_order_relaxed)) == 1;
    const auto readBand = [&] (dsp::BandSettings& b, const Band& src, size_t i)
    {
        b.frequency = f (src.freq);
        b.gainDb = g (src.gain, src.gainParam);
        b.q = f (src.q);
        b.shape = dsp::Shape::bell;
        if (src.type != nullptr)
        {
            const int t = juce::roundToInt (src.type->load (std::memory_order_relaxed));
            if (t == 1) b.shape = i == 0 ? dsp::Shape::lowShelf : dsp::Shape::highShelf;
            if (t == 2) b.shape = i == 0 ? dsp::Shape::lowCut : dsp::Shape::highCut;
        }
    };
    for (size_t i = 0; i < 4; ++i)
        readBand (s.bands[i], bands[i], i);
    if (eqMs != nullptr)
    {
        s.msEq = on (eqMs);
        for (size_t i = 0; i < 4; ++i)
            readBand (s.sideBands[i], sideBands[i], i);
    }
    s.dynOn = on (dynOn);
    s.comp.thresholdDb = g (threshold, thresholdParam);
    s.comp.ratio = f (ratio);
    s.comp.attackMs = f (attack);
    s.comp.releaseMs = f (release);
    s.comp.autoRelease = on (autoRelease);
    s.comp.knee = f (knee);
    s.comp.makeupDb = g (makeup, makeupParam);
    s.comp.autoMakeup = on (autoMakeup);
    s.comp.mix = f (mix) / 100.0;
    s.comp.sidechainHpfHz = f (scHpf) <= 20.5 ? 0.0 : f (scHpf);
    s.compFirst = juce::roundToInt (order->load (std::memory_order_relaxed)) == 1;
    s.midDb = g (mid, midParam);
    s.sideDb = g (side, sideParam);
    s.balance = f (balance) / 100.0;
    s.mono = on (mono);
    s.outputDb = g (output, outputParam);
    if (eqOutput != nullptr)
    {
        // 0 dB must be exact, so that the control can't change a sample at rest. The range's 0.01 dB grid, computed
        // in float, doesn't land exactly on 0, and no valid value lies this close to it.
        const double db = g (eqOutput, eqOutputParam);
        s.eqOutputDb = std::abs (db) < 0.005 ? 0.0 : db;
    }
    return s;
}

//==============================================================================
juce::String describe (const juce::String& pid)
{
    if (pid.startsWith (id::focusPrefix)) return "For control surfaces: this EQ control on the selected channel.";
    if (pid.endsWith ("_gain"))  return "Band gain. Drag, or Shift-drag for fine control. Double-click to reset.";
    if (pid.endsWith ("_freq"))  return "Band frequency. Click the value to type an exact number (e.g. 2.5k).";
    if (pid.endsWith ("_q"))     return "Bandwidth. Higher Q is narrower. On shelves it shapes the knee; on cuts, the resonance.";
    if (pid.endsWith ("_type"))  return "Band shape: bell, shelf or 12 dB/oct cut.";
    if (pid == id::eqOn)         return "EQ in or out (click-free).";
    if (pid == id::eqMs)         return "Stereo, or independent Mid and Side EQ.";
    if (pid == id::eqChannel)    return "In M/S: which channel the EQ controls show, Mid or Side. Each keeps its own settings.";
    if (pid == id::eqOutput)     return "EQ Output: level matching after EQ moves. After the compressor, so it never changes what the compressor hears. Follows EQ in.";
    if (pid == id::eqMode)       return "EQ mode: COHERENCE (zero latency) or WEISS (the Weiss EQ MP model at 2x, 15 samples latency).";
    if (pid == id::dynOn)        return "Compressor in or out (click-free). The detector keeps running while out.";
    if (pid == id::threshold)    return "Compressor threshold. Below it the signal is untouched.";
    if (pid == id::ratio)        return "Ratio, from 1:1 to infinity. Most of the travel covers 1.5:1 to 6:1.";
    if (pid == id::attack)       return "Attack time of the level detector.";
    if (pid == id::release)      return "Release time of the level detector.";
    if (pid == id::autoRelease)  return "Program-dependent release: fast after transients, slow on sustained material.";
    if (pid == id::knee)         return "Knee width in dB, centred on the threshold: 0 is a hard knee; up to 72 dB for a very gradual onset.";
    if (pid == id::makeup)       return "Makeup gain after compression.";
    if (pid == id::autoMakeup)   return "Auto makeup: restores most of the level the compressor takes away, measured on the audio, and nothing while it isn't compressing. Trim with Makeup.";
    if (pid == id::mix)          return "Parallel compression: blend of compressed and uncompressed signal.";
    if (pid == id::scHpf)        return "High-pass filter on the detector only, so low end doesn't drive the compressor.";
    if (pid == id::order)        return "Processing order of EQ and dynamics.";
    if (pid == id::input)        return "Input trim, before all processing.";
    if (pid == id::mid)          return "Level of the Mid (L+R) signal.";
    if (pid == id::side)         return "Level of the Side (L-R) signal.";
    if (pid == id::msLink)       return "Link: Mid and Side move together, keeping their offset.";
    if (pid == id::balance)      return "Left/right balance: turns the opposite channel down.";
    if (pid == id::mono)         return "Mono monitoring. Affects only what you hear, not the processing or meters.";
    if (pid == id::output)       return "Output level. Double-click for 0 dB.";
    if (pid == id::bypass)       return "Power: bypasses the whole strip (click-free).";
    return {};
}
} // namespace coherence
