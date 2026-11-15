#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace coherence
{
namespace
{
const juce::Identifier uiScaleId { "uiScale" };
const juce::Identifier abSlotId { "abSlot" };
const juce::Identifier otherSlotId { "OTHER_SLOT" };
const juce::Identifier otherSlotFreshId { "otherSlotFresh" };
const juce::Identifier extId { "ext" };

// A set from before a parameter existed loads it at its default, whatever this instance held. (The value tree state
// would otherwise leave that parameter as it is.)
void addMissingDefaults (juce::ValueTree& params, const juce::AudioProcessor& proc)
{
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && isSavedParameter (*rp)
                                                                      && ! params.getChildWithProperty ("id", rp->getParameterID()).isValid())
        {
            juce::ValueTree c ("PARAM");
            c.setProperty ("id", rp->getParameterID(), nullptr);
            c.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
            params.appendChild (c, nullptr);
        }
}

// A set from before the Weiss mode existed ran in what is now COHERENCE mode. It stays there: only new instances
// start in Weiss mode.
void addOlderEqMode (juce::ValueTree& params)
{
    if (params.getChildWithProperty ("id", id::eqMode).isValid())
        return;
    juce::ValueTree c ("PARAM");
    c.setProperty ("id", id::eqMode, nullptr);
    c.setProperty ("value", 0.0f, nullptr);
    params.appendChild (c, nullptr);
}

// A value that isn't a finite number (NaN or an infinity: a damaged file, or a set saved after a host wrote NaN to a
// parameter) loads that parameter at its default, as a set from before the parameter existed does. Left in, a NaN made
// the parameter NaN: the engine then outputs silence (DESIGN.md section 21), the readout says "nan", and the next save
// writes it again. The test is on the float the parameter would be given, so a number too large for a float counts.
// Finite values are never touched, in range or out: out of range they go to the nearest end, as they always have.
void replaceNonFiniteValues (juce::ValueTree& params, const juce::AudioProcessor& proc)
{
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && isSavedParameter (*rp))
            if (auto c = params.getChildWithProperty ("id", rp->getParameterID());
                c.isValid() && c.hasProperty ("value") && ! std::isfinite ((float) c.getProperty ("value")))
            {
                c.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                c.removeProperty (extId, nullptr);   // the default is the plug-in's own value, not one from outside
            }
}

juce::ValueTree parametersOnly (const juce::ValueTree& state)
{
    juce::ValueTree t (state.getType());
    for (const auto& child : state)
        if (child.hasType ("PARAM"))
            t.appendChild (child.createCopy(), nullptr);
    return t;
}
} // namespace

CoherenceProcessor::CoherenceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "COHERENCE", createParameterLayout()),
      reader (apvts)
{
    auto meter = createGainReductionMeter();
    grMeter = meter.get();
    addParameter (meter.release());
    // After the meter, so that no existing index moves. The state tree is rebuilt because the value tree state only
    // takes new parameters without one; nothing has been set yet, so this only records the defaults.
    {
        const auto stateType = apvts.state.getType();
        apvts.state = {};
        addAppendedParameters (apvts);
        apvts.state = juce::ValueTree (stateType);
        // Every parameter's entry holds its value from the start. The value tree state writes a first value only once,
        // and for the parameters of the original layout that happened before this rebuild, into entries that are gone.
        // An entry without a value gets its first one outside the undo manager, so in a new instance the first move
        // of each of those controls wasn't recorded, and UNDO took back an earlier move of another control instead
        // (v0.5.0 to v0.6.1). The values written here are the ones the parameters already have.
        for (auto child : apvts.state)
            if (! child.hasProperty ("value"))
                if (const auto* value = apvts.getRawParameterValue (child.getProperty ("id").toString()))
                    child.setProperty ("value", value->load(), nullptr);
        undoManager.clearUndoHistory();
    }
    reader.attachAppended (apvts);
    eqFocus = std::make_unique<EqFocus> (*this, apvts);
    // Last of all, so that no existing index moves.
    auto selector = createAbSelectParameter();
    abParam = selector.get();
    addParameter (selector.release());
    abParam->addListener (this);
    otherSlot = parameterSnapshot();
    // From the start, because a host may read the latency before it prepares the plug-in.
    reportedWeiss = reader.read().weissEq;
    setLatencySamples (reportedWeiss ? dsp::weissLatencySamples : 0);
    undoManager.addChangeListener (this);
}

CoherenceProcessor::~CoherenceProcessor()
{
    undoManager.removeChangeListener (this);
    cancelPendingUpdate();
    abParam->removeListener (this);
}

// Only a gesture on the panel starts a new undo step. Values the host writes (automation, a control surface) are
// recorded by the value tree state into whichever step is open, and when two or more parameters move in turn their
// entries can't be merged. That step then grew for as long as the automation played, with the panel open or closed:
// about 85 entries a second for two parameters, never released while the instance lived, and one press of UNDO took
// the automated controls back to where they were when it began (measured on v0.6.1, mac-results/v0.6.2). A step far
// larger than any gesture is automation, not editing, so the history starts again. Nothing else changes: a gesture
// is still one step, and automation of a single parameter is still one entry.
void CoherenceProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (undoManager.getNumActionsInCurrentTransaction() > maxUndoActionsPerStep)
        undoManager.clearUndoHistory();
}

juce::ValueTree CoherenceProcessor::parameterSnapshot() const
{
    juce::ValueTree state ("COHERENCE");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && isSavedParameter (*rp))
        {
            juce::ValueTree c ("PARAM");
            c.setProperty ("id", rp->getParameterID(), nullptr);
            c.setProperty ("value", rp->convertFrom0to1 (rp->getValue()), nullptr);
            if (auto* sp = dynamic_cast<SteppedGainParameter*> (rp); sp != nullptr && sp->isExternal())
                c.setProperty (extId, 1, nullptr);
            state.appendChild (c, nullptr);
        }
    return state;
}

void CoherenceProcessor::applyExternalFlags (const juce::ValueTree& params)
{
    // Every dB parameter is set: one missing from the tree (an older set) plays exactly as stored.
    for (auto* p : getParameters())
        if (auto* sp = dynamic_cast<SteppedGainParameter*> (p))
        {
            const auto c = params.getChildWithProperty ("id", sp->getParameterID());
            sp->setExternal (c.isValid() && (int) c.getProperty (extId, 0) == 1);
        }
}

void CoherenceProcessor::undoStep (bool redo)
{
    // The value tree state records changed values on a timer, which slows to twice a second while nothing moves. A move
    // it hasn't recorded yet is recorded now (copyState() does that first): otherwise UNDO passed over it and took back
    // the move before it.
    (void) apvts.copyState();
    const ScopedWriteSource src (WriteSource::internal);
    if (redo)
        undoManager.redo();
    else
        undoManager.undo();
}

bool CoherenceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void CoherenceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    reportedWeiss = reader.read().weissEq;
    setLatencySamples (reportedWeiss ? dsp::weissLatencySamples : 0);
    grBallistics.reset();
    meterData.hostGainReductionDb = 0.0f;
    preparedRate = sampleRate;
}

void CoherenceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    for (int c = numIn; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const auto settings = reader.read();
    if (settings.weissEq != reportedWeiss)
    {
        // EQ mode changed: tell the host the new latency (the engine fades around the switch).
        reportedWeiss = settings.weissEq;
        setLatencySamples (reportedWeiss ? dsp::weissLatencySamples : 0);
    }
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (numIn, buffer.getNumChannels()), buffer.getNumSamples(),
                    settings, meterData);

    // The control-surface meter: sent only when the reading moves (about 0.01 dB near 0), so an idle compressor
    // sends nothing.
    const float fs = (float) preparedRate;
    if (fs > 0.0f)
    {
        const float reading = grBallistics.process (meterData.hostGainReductionDb.exchange (0.0f), (float) buffer.getNumSamples() / fs);
        const float v = gainReductionToMeterValue (reading);
        if (std::abs (v - grMeter->get()) > 1.0e-4f || (juce::exactlyEqual (v, 0.0f) && ! juce::exactlyEqual (grMeter->get(), 0.0f)))
            grMeter->setValueNotifyingHost (v);
    }
}

juce::AudioProcessorParameter* CoherenceProcessor::getBypassParameter() const
{
    return apvts.getParameter (id::bypass);
}

juce::AudioProcessorEditor* CoherenceProcessor::createEditor()
{
    return new CoherenceEditor (*this);
}

//==============================================================================
void CoherenceProcessor::selectAbSlot (int slot)
{
    if (slot == abSlot.load())
        return;
    const auto current = parameterSnapshot();
    juce::ValueTree target;
    bool startsAsCopy = false;
    {
        const juce::ScopedLock sl (slotLock);
        startsAsCopy = otherSlotFresh;
        otherSlotFresh = false;
        target = otherSlot;
        otherSlot = current;
        abSlot = slot;
    }
    // Apply the other slot parameter by parameter, so the host sees normal parameter changes. A slot with no settings of
    // its own starts as a copy of this one, so nothing moves.
    if (! startsAsCopy)
    {
        {
            const ScopedWriteSource src (WriteSource::internal);
            for (const auto& child : target)
            {
                if (auto* p = apvts.getParameter (child.getProperty ("id").toString()))
                {
                    const float v = (float) child.getProperty ("value");
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
                    p->endChangeGesture();
                }
            }
        }
        applyExternalFlags (target);
    }
    undoManager.clearUndoHistory();
    syncAbParameter();
}

void CoherenceProcessor::toggleAbSlot()
{
    selectAbSlot (abSlot.load() == 0 ? 1 : 0);
}

void CoherenceProcessor::parameterValueChanged (int, float newValue)
{
    if (ScopedWriteSource::current() != WriteSource::host)
        return;   // syncAbParameter()'s own write

    const int slot = newValue >= 0.5f ? 1 : 0;
    constexpr juce::uint32 echoWindowMs = 300;   // how long after our own write a host may still pass it back
    const bool echo = juce::Time::getMillisecondCounter() - lastAbWriteMs.load (std::memory_order_relaxed) < echoWindowMs
                      && (abWrittenMask.load (std::memory_order_relaxed) & (1 << slot)) != 0;
    if (! echo)
        pendingAbSlot.store (slot, std::memory_order_relaxed);
    triggerAsyncUpdate();   // an echo can leave the parameter away from the slot, so this also brings it back
}

void CoherenceProcessor::handleAsyncUpdate()
{
    const int requested = pendingAbSlot.exchange (-1, std::memory_order_relaxed);
    if (requested >= 0)
        selectAbSlot (requested);
    syncAbParameter();
}

void CoherenceProcessor::syncAbParameter()
{
    const int slot = abSlot.load();
    if (abParam->getIndex() == slot)
        return;
    constexpr juce::uint32 echoWindowMs = 300;
    const auto now = juce::Time::getMillisecondCounter();
    const int mask = now - lastAbWriteMs.load (std::memory_order_relaxed) < echoWindowMs ? abWrittenMask.load (std::memory_order_relaxed) : 0;
    abWrittenMask.store (mask | (1 << slot), std::memory_order_relaxed);
    lastAbWriteMs.store (now, std::memory_order_relaxed);
    const ScopedWriteSource src (WriteSource::internal);
    abParam->setValueNotifyingHost ((float) slot);
}

void CoherenceProcessor::copyCurrentToOtherSlot()
{
    auto copy = parameterSnapshot();
    const juce::ScopedLock sl (slotLock);
    otherSlot = copy;
    otherSlotFresh = false;
}

float CoherenceProcessor::getUiScale() const
{
    return uiScale.load();
}

void CoherenceProcessor::setUiScale (float s)
{
    uiScale = s;
}

void CoherenceProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // Built from the parameters' current values (atomic reads), not with apvts.copyState(): copyState() first
    // flushes pending values into the shared ValueTree through the UndoManager, on whatever thread the host
    // calls from. A host saving from a background thread while the UI switches A/B or uses undo then has two
    // threads inside the UndoManager (found by the v0.2.2 audit as a crash). The result has the same layout as
    // copyState(): one PARAM child per parameter with "id" and "value" (plus "ext", see parameterSnapshot()).
    auto state = parameterSnapshot();
    if (uiScale.load() > 0.0f)
        state.setProperty (uiScaleId, uiScale.load(), nullptr);
    state.setProperty (abSlotId, abSlot.load(), nullptr);
    juce::ValueTree other (otherSlotId);
    {
        const juce::ScopedLock sl (slotLock);
        other.appendChild (otherSlot.createCopy(), nullptr);
        if (otherSlotFresh)
            state.setProperty (otherSlotFreshId, true, nullptr);
    }
    state.appendChild (other, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void CoherenceProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType()))
            return;
        addOlderEqMode (state);
        addMissingDefaults (state, *this);
        replaceNonFiniteValues (state, *this);
        auto other = state.getChildWithName (otherSlotId);
        {
            const juce::ScopedLock sl (slotLock);
            // A state without a second slot (older or host presets) starts B as a copy of A, rather than keeping
            // whatever B held before the load. A set whose other slot holds settings keeps them, whatever they are.
            const bool hasOther = other.isValid() && other.getNumChildren() > 0;
            otherSlot = hasOther ? other.getChild (0).createCopy() : parametersOnly (state);
            otherSlotFresh = ! hasOther || (bool) state.getProperty (otherSlotFreshId, false);
            addOlderEqMode (otherSlot);
            addMissingDefaults (otherSlot, *this);
            replaceNonFiniteValues (otherSlot, *this);   // these values reach the parameters when the slot is selected
        }
        state.removeChild (other, nullptr);
        abSlot = (int) state.getProperty (abSlotId, 0) == 1 ? 1 : 0;
        pendingAbSlot.store (-1, std::memory_order_relaxed);
        // A window scale that isn't a finite number counts as not chosen (the editor then uses the user's default).
        const float savedScale = (float) state.getProperty (uiScaleId, -1.0f);
        uiScale = std::isfinite (savedScale) ? savedScale : -1.0f;
        {
            const ScopedWriteSource src (WriteSource::internal);
            apvts.replaceState (state);
        }
        // The set's mode, straight away: a host may read the latency before the next block.
        setLatencySamples (apvts.getRawParameterValue (id::eqMode)->load() >= 0.5f ? dsp::weissLatencySamples : 0);
        applyExternalFlags (state);
        undoManager.clearUndoHistory();
        triggerAsyncUpdate();   // the A/B parameter follows, on the message thread
    }
}
} // namespace coherence

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new coherence::CoherenceProcessor();
}
