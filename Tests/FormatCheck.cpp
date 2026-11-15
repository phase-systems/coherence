// COHERENCE - format consistency check (run by build.sh on the Mac after installing).
//
// Loads the installed AU and VST3 exactly as a DAW does (through the plug-in format APIs), sets identical
// parameters on both, processes identical audio with automation, and compares the outputs sample by sample.
// It also checks recall: each format's saved state is loaded into a fresh instance and must reproduce both
// the parameter values and the audio.
//
//   CoherenceFormatCheck <report.md> [path to .vst3] [AU identifier]

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>
#include <random>

namespace
{
juce::String report;
int failures = 0;

void line (const juce::String& s)
{
    std::printf ("%s\n", s.toRawUTF8());
    report << s << "\n";
}

void check (bool ok, const juce::String& what)
{
    line (juce::String (ok ? "- PASS  " : "- FAIL  ") + what);
    if (! ok)
        ++failures;
}

std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& fm, const juce::String& formatName,
                                                 const juce::String& fileOrId, double sr, int block, juce::String& error)
{
    for (auto* format : fm.getFormats())
    {
        if (format->getName() != formatName)
            continue;
        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, fileOrId);
        if (found.isEmpty())
        {
            error = "not found: " + fileOrId;
            return nullptr;
        }
        return fm.createPluginInstance (*found[0], sr, block, error);
    }
    error = "format not available: " + formatName;
    return nullptr;
}

std::map<juce::String, juce::AudioProcessorParameter*> paramsByName (juce::AudioProcessor& p)
{
    std::map<juce::String, juce::AudioProcessorParameter*> m;
    for (auto* param : p.getParameters())
        m[param->getName (128)] = param;
    return m;
}

// Deterministic "session": parameter values (normalised) chosen to exercise every section.
std::vector<std::pair<juce::String, float>> sessionValues (int variant)
{
    std::mt19937 rng ((unsigned) (1234 + variant));
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::vector<std::pair<juce::String, float>> v;
    for (const char* name : { "LF Gain", "LF Freq", "LF Q", "LMF Gain", "LMF Freq", "LMF Q", "HMF Gain", "HMF Freq", "HMF Q",
                              "HF Gain", "HF Freq", "HF Q", "Threshold", "Ratio", "Attack", "Release", "Knee", "Makeup",
                              "Mix", "SC HPF", "Input", "Mid Level", "Side Level", "Balance", "Output" })
        v.push_back ({ name, u (rng) });
    v.push_back ({ "LF Type", variant == 0 ? 0.5f : 1.0f });   // shelf / cut
    v.push_back ({ "HF Type", variant == 0 ? 0.0f : 0.5f });   // bell / shelf
    v.push_back ({ "Dyn In", 1.0f });
    v.push_back ({ "EQ Mode", variant == 0 ? 0.0f : 1.0f });   // both modes: a new instance alone would only test Weiss
    v.push_back ({ "Order", variant == 0 ? 0.0f : 1.0f });
    v.push_back ({ "Auto Release", variant == 0 ? 0.0f : 1.0f });
    v.push_back ({ "Mono", 0.0f });
    v.push_back ({ "Bypass", 0.0f });
    return v;
}

bool apply (juce::AudioProcessor& p, const std::vector<std::pair<juce::String, float>>& values, juce::String& missing)
{
    auto m = paramsByName (p);
    for (const auto& [name, value] : values)
    {
        auto it = m.find (name);
        if (it == m.end())
        {
            missing << name << " ";
            continue;
        }
        it->second->beginChangeGesture();
        it->second->setValueNotifyingHost (value);
        it->second->endChangeGesture();
    }
    return missing.isEmpty();
}

std::vector<std::vector<float>> render (juce::AudioProcessor& p, const std::vector<std::vector<float>>& in, int block, bool automate)
{
    auto out = in;
    const int n = (int) in[0].size();
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    auto m = paramsByName (p);
    int b = 0;
    for (int start = 0; start < n; start += block, ++b)
    {
        const int len = std::min (block, n - start);
        if (automate)
        {
            // Host automation: a slow sweep on a few parameters, one value per block (same on both formats).
            const float t = (float) start / (float) n;
            for (auto [name, v] : { std::pair { "HMF Freq", 0.2f + 0.6f * t }, std::pair { "LMF Gain", 0.3f + 0.4f * t },
                                    std::pair { "Side Level", 0.5f + 0.2f * std::sin (t * 6.0f) } })
                if (auto it = m.find (name); it != m.end())
                    it->second->setValueNotifyingHost (v);
        }
        juce::AudioBuffer<float> chunk (2, len);
        for (int c = 0; c < 2; ++c)
            chunk.copyFrom (c, 0, in[(size_t) c].data() + start, len);
        p.processBlock (chunk, midi);
        for (int c = 0; c < 2; ++c)
            std::copy (chunk.getReadPointer (c), chunk.getReadPointer (c) + len, out[(size_t) c].begin() + start);
    }
    return out;
}

double maxDiff (const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b)
{
    double m = 0;
    for (size_t c = 0; c < a.size(); ++c)
        for (size_t i = 0; i < a[c].size(); ++i)
            m = std::max (m, (double) std::abs (a[c][i] - b[c][i]));
    return m;
}

juce::String db (double v) { return v <= 0 ? juce::String ("bit-identical") : juce::String (20.0 * std::log10 (v), 1) + " dBFS"; }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File reportFile (argc > 1 ? juce::String (argv[1]) : juce::String ("format-check.md"));
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const juce::String vst3 = argc > 2 ? juce::String (argv[2]) : home.getChildFile ("Library/Audio/Plug-Ins/VST3/COHERENCE.vst3").getFullPathName();
    const juce::String auId = argc > 3 ? juce::String (argv[3]) : juce::String ("AudioUnit:Effects/aufx,Cohr,Phsy");

    juce::AudioPluginFormatManager fm;
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
#endif
#if JUCE_PLUGINHOST_VST3
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
#endif

    line ("# COHERENCE format check");
    line ("AU and VST3 loaded through the plug-in hosting APIs, the way a DAW loads them.\n");

    const int block = 512;
    std::mt19937 rng (99);
    std::normal_distribution<float> nd (0.0f, 0.15f);

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        line ("## " + juce::String (sr / 1000.0, 1) + " kHz\n");
        const int n = (int) sr * 6;
        std::vector<std::vector<float>> in (2, std::vector<float> ((size_t) n));
        for (auto& c : in)
            for (auto& v : c)
                v = nd (rng);

        juce::String errA, errV;
        auto vst = load (fm, "VST3", vst3, sr, block, errV);
#if JUCE_MAC
        auto au = load (fm, "AudioUnit", auId, sr, block, errA);
#else
        std::unique_ptr<juce::AudioPluginInstance> au;
        errA = "AU hosting is macOS only (this run compares two VST3 instances)";
        if (vst != nullptr)
            au = load (fm, "VST3", vst3, sr, block, errA);
#endif
        check (vst != nullptr, "VST3 loads" + (vst ? juce::String() : ": " + errV));
        check (au != nullptr, "AU loads" + (au ? juce::String() : ": " + errA));
        if (vst == nullptr || au == nullptr)
            continue;

        for (auto* p : { vst.get(), au.get() })
        {
            p->setPlayConfigDetails (2, 2, sr, block);
            p->prepareToPlay (sr, block);
        }
        check (vst->getLatencySamples() == 15 && au->getLatencySamples() == 15, "a new instance starts in Weiss mode: both report 15 samples latency");
        check (vst->getParameters().size() == au->getParameters().size(),
               "same parameter set (" + juce::String (vst->getParameters().size()) + " / " + juce::String (au->getParameters().size()) + ")");

        for (int variant = 0; variant < 2; ++variant)
        {
            const auto values = sessionValues (variant);
            juce::String missV, missA;
            apply (*vst, values, missV);
            apply (*au, values, missA);
            check (missV.isEmpty() && missA.isEmpty(), "every parameter found by name" + ((missV + missA).trim().isEmpty() ? juce::String() : " (missing: " + (missV + missA).trim() + ")"));

            // Parameter values as each format reports them (text, which is what the DAW displays).
            auto mv = paramsByName (*vst), ma = paramsByName (*au);
            int textMismatch = 0;
            juce::String firstText;
            for (const auto& [name, param] : mv)
                if (name == "Gain Reduction")
                    continue;   // the read-only meter for control surfaces is an output; it differs between the test instances
                else if (auto it = ma.find (name); it != ma.end() && param->getCurrentValueAsText() != it->second->getCurrentValueAsText())
                {
                    if (textMismatch++ == 0)
                        firstText = name + ": " + param->getCurrentValueAsText() + " vs " + it->second->getCurrentValueAsText();
                }
            check (textMismatch == 0, "variant " + juce::String (variant + 1) + ": both formats display the same value for every parameter"
                                          + (textMismatch ? " (first difference " + firstText + ")" : juce::String()));

            const auto yV = render (*vst, in, block, true);
            const auto yA = render (*au, in, block, true);
            const double d = maxDiff (yV, yA);
            check (d < 1.0e-6, "variant " + juce::String (variant + 1) + ": AU and VST3 output with automation: max difference " + db (d));

            // Recall: save each format's state, load it into a fresh instance, compare parameters and audio.
            for (auto [name, inst, file] : { std::tuple { juce::String ("VST3"), vst.get(), vst3 }, std::tuple { juce::String ("AU"), au.get(), auId } })
            {
                juce::MemoryBlock state;
                inst->getStateInformation (state);
                juce::String err;
#if JUCE_MAC
                auto fresh = load (fm, name == "AU" ? "AudioUnit" : "VST3", file, sr, block, err);
#else
                auto fresh = load (fm, "VST3", vst3, sr, block, err);
#endif
                if (fresh == nullptr)
                {
                    check (false, name + " recall: could not create a second instance (" + err + ")");
                    continue;
                }
                fresh->setPlayConfigDetails (2, 2, sr, block);
                fresh->prepareToPlay (sr, block);
                fresh->setStateInformation (state.getData(), (int) state.getSize());
                int paramMismatch = 0;
                juce::String firstDiff;
                auto a = paramsByName (*inst), b = paramsByName (*fresh);
                for (const auto& [pn, param] : a)
                    if (pn == "Gain Reduction")
                        continue;   // the read-only meter for control surfaces (v0.2.4) is an output, not part of the set
                    else if (auto it = b.find (pn); it == b.end() || param->getCurrentValueAsText() != it->second->getCurrentValueAsText())
                        if (paramMismatch++ == 0)
                            firstDiff = pn + ": " + param->getCurrentValueAsText() + " vs " + (it == b.end() ? juce::String ("missing") : it->second->getCurrentValueAsText());
                check (paramMismatch == 0, name + " recall: every parameter restored (" + juce::String (paramMismatch) + " differences"
                                               + (paramMismatch ? ", first " + firstDiff : juce::String()) + ")");
                // Same audio from the recalled instance and from a fresh instance given the same values as
                // ordinary parameter changes (both start from a clean state, without automation).
#if JUCE_MAC
                auto ref = load (fm, name == "AU" ? "AudioUnit" : "VST3", file, sr, block, err);
#else
                auto ref = load (fm, "VST3", vst3, sr, block, err);
#endif
                if (ref == nullptr)
                {
                    check (false, name + " recall: could not create a reference instance (" + err + ")");
                    continue;
                }
                ref->setPlayConfigDetails (2, 2, sr, block);
                ref->prepareToPlay (sr, block);
                {
                    auto rp = paramsByName (*ref);
                    for (const auto& [pn, param] : a)
                        if (auto it = rp.find (pn); it != rp.end())
                            it->second->setValueNotifyingHost (param->getValue());
                }
                const auto y1 = render (*ref, in, block, false);
                const auto y2 = render (*fresh, in, block, false);
                const double dr = maxDiff (y1, y2);
                if (dr >= 1.0e-6)
                {
                    int firstBad = -1;
                    for (size_t i = 0; i < y1[0].size() && firstBad < 0; ++i)
                        if (std::abs (y1[0][i] - y2[0][i]) > 1.0e-6f) firstBad = (int) i;
                    line ("  (first difference at sample " + juce::String (firstBad) + ")");
                }
                check (dr < 1.0e-6, name + " recall: the recalled instance sounds identical to one set up by hand (" + db (dr) + ")");
            }
        }
    }

    line ("\n" + juce::String (failures == 0 ? "All format checks passed." : juce::String (failures) + " format check(s) FAILED."));
    reportFile.replaceWithText (report);
    return failures == 0 ? 0 : 1;
}
