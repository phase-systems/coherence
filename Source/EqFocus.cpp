#include "EqFocus.h"

namespace coherence
{
namespace
{
constexpr float sameValue = 2.0e-5f;            // normalised
constexpr juce::uint32 echoWindowMs = 300;      // how long after sync() a host may still pass its write back
constexpr juce::uint32 surfaceSettleMs = 150;   // sync() leaves a control alone this long after the surface moved it

// Moving a focus parameter moves another parameter. Audio Units have to declare that as a meta parameter, or auval fails
// and hosts can't trust the values they saved. Only the AU uses this flag; the VST3 that 360 Link hosts doesn't see it.
class FocusParameter final : public juce::AudioParameterFloat
{
public:
    using juce::AudioParameterFloat::AudioParameterFloat;
    bool isMetaParameter() const override { return true; }
};
} // namespace

EqFocus::EqFocus (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state)
{
    msOn = state.getRawParameterValue (id::eqMs);
    sideSelected = state.getRawParameterValue (id::eqChannel);

    int i = 0;
    for (int b = 0; b < 4; ++b)
    {
        const auto& band = id::bands[b];
        const auto side = id::sideBand (band);
        const std::pair<juce::String, juce::String> controls[] = { { id::bandGain (band), id::bandGain (side) },
                                                                   { id::bandFreq (band), id::bandFreq (side) },
                                                                   { id::bandQ (band), id::bandQ (side) } };
        for (const auto& [mainId, sideId] : controls)
        {
            auto& e = entries[(size_t) i];
            e.main = state.getParameter (mainId);
            e.side = state.getParameter (sideId);
            jassert (e.main != nullptr && e.side != nullptr);
            for (auto& r : e.recent)
                r.store (std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);

            // The readout and typed values are the selected channel's, so stepping shows as it does on that channel.
            auto* entry = &e;
            auto attributes = juce::AudioParameterFloatAttributes()
                                  .withAutomatable (false)
                                  .withLabel (e.main->getLabel())
                                  .withStringFromValueFunction ([this, entry] (float v, int)
                                  {
                                      auto* q = selected (*entry);
                                      return q->getText (q->convertTo0to1 (v), 0);
                                  })
                                  .withValueFromStringFunction ([this, entry] (const juce::String& text)
                                  {
                                      auto* q = selected (*entry);
                                      return q->convertFrom0to1 (q->getValueForText (text));
                                  });
            auto parameter = std::make_unique<FocusParameter> (
                juce::ParameterID { juce::String (id::focusPrefix) + mainId, 1 }, "Focus " + e.main->getName (64),
                e.main->getNormalisableRange(), e.main->convertFrom0to1 (e.main->getDefaultValue()), attributes);
            e.focus = parameter.get();
            processor.addParameter (parameter.release());
            e.focus->addListener (this);
            ++i;
        }
    }
    firstIndex = entries[0].focus->getParameterIndex();
    startTimerHz (30);
}

EqFocus::~EqFocus()
{
    stopTimer();
    for (auto& e : entries)
        e.focus->removeListener (this);
}

juce::RangedAudioParameter* EqFocus::selected (const Entry& e) const noexcept
{
    const bool side = msOn->load (std::memory_order_relaxed) >= 0.5f && sideSelected->load (std::memory_order_relaxed) >= 0.5f;
    return side ? e.side : e.main;
}

void EqFocus::parameterValueChanged (int parameterIndex, float newValue)
{
    if (ScopedWriteSource::current() != WriteSource::host)
        return;   // sync()'s own write

    auto& e = entries[(size_t) (parameterIndex - firstIndex)];
    const auto now = juce::Time::getMillisecondCounter();
    if (now - e.lastSyncMs.load (std::memory_order_relaxed) < echoWindowMs)
        for (const auto& r : e.recent)
            if (std::abs (r.load (std::memory_order_relaxed) - newValue) < sameValue)   // NaN (empty) never matches
                return;

    e.lastOutsideMs.store (now, std::memory_order_relaxed);
    // Still from the host, as far as the channel's parameter can tell, so a dB control plays on the step grid.
    selected (e)->setValueNotifyingHost (newValue);
}

void EqFocus::sync()
{
    const ScopedWriteSource source (WriteSource::internal);
    const auto now = juce::Time::getMillisecondCounter();
    for (auto& e : entries)
    {
        const auto outside = e.lastOutsideMs.load (std::memory_order_relaxed);
        if (outside != 0 && now - outside < surfaceSettleMs)
            continue;
        const float target = selected (e)->getValue();
        if (std::abs (target - e.focus->getValue()) < sameValue)
            continue;
        const int head = e.recentHead.load (std::memory_order_relaxed);
        e.recent[(size_t) head].store (target, std::memory_order_relaxed);
        e.recentHead.store ((head + 1) % Entry::recentSize, std::memory_order_relaxed);
        e.lastSyncMs.store (now, std::memory_order_relaxed);
        e.focus->setValueNotifyingHost (target);
    }
}
} // namespace coherence
