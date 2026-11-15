// PlugHost: load an installed AU/VST3 headlessly (as a DAW does) to dump its parameters or render a WAV through it.
// Used for the FabFilter Pro-C 3 reference renders (COMPRESSOR-TESTING.md). Test tooling only; not part of the plug-in.
//   PlugHost dump <format VST3|AU> <path or id>
//   PlugHost render <format> <path> <in.wav> <out.wav> "Name=text" ...   (value set by matching display text)
//   PlugHost encoder <format> <path> <parameter name> <delta> <clicks>
//       moves a parameter from the host side, in steps, with audio running, and prints the readout and output level
//   PlugHost focus-check <format> <path>
//       COHERENCE's focus parameters in a real host: the selector, forwarding, sync back and host echoes
//   PlugHost meter <format> <path> <meter parameter name> "Name=text" ...
//       plays a 1 kHz square at -6 dBFS for 1 s, then silence for 1 s, in 512-sample blocks, and prints the meter
//       parameter as the host sees it (value and text) every 4 blocks: checks a read-only output parameter path.
//   PlugHost ab-check <format> <path>
//       COHERENCE's A/B switch in a real host: pressing it, independent settings, and recall. Exit code 0 = all pass.
//   PlugHost nan-check <format> <path>
//       COHERENCE in a real host: a NaN or Inf input sample in each EQ mode, stereo and M/S. Exit code 0 = all recover.
//   PlugHost state-save <format> <path> <state file>
//       sets every automatable parameter to a value that depends only on its name, saves the state, and prints every readout
//   PlugHost state-load <format> <path> <state file>
//       loads a state and prints every readout, to compare with what the build that saved it printed
//   PlugHost link-walk <format> <SSL 360 Link path> <out folder> [<component state in> [<script>]]
//       Saves 360 Link's component state as base.state. With a state in, also moves each slot in turn and saves the state
//       after each move as <index>.state, so that a script can see which hosted parameter each slot really moves
//       (Tools/ssl360link/verify_in_link.py). A script as a further argument runs instead of the walk.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
static std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& fm, juce::String fmt, juce::String id, double sr, int bs)
{
    for (auto* f : fm.getFormats()) if (f->getName() == fmt) {
        juce::OwnedArray<juce::PluginDescription> found; f->findAllTypesForFile (found, id);
        if (found.isEmpty()) { std::printf ("not found\n"); return nullptr; }
        juce::String err; auto p = fm.createPluginInstance (*found[0], sr, bs, err);
        if (! p) std::printf ("error: %s\n", err.toRawUTF8());
        return p; }
    return nullptr;
}
// Numeric value of a display text, in the unit of the plug-in's smaller unit (ms for "sec", Hz for "kHz").
static double textNumber (const juce::String& t)
{
    const double v = t.getDoubleValue();
    if (t.containsIgnoreCase ("sec") || t.trim().endsWithIgnoreCase (" s")) return v * 1000.0;
    if (t.containsIgnoreCase ("kHz")) return v * 1000.0;
    return v;
}
// Normalised value whose display text matches `want`: an exact text match, else the value displayed nearest
// to the number. The middle of the run of normalised values that display it is returned, so the value set is
// centred in the display's resolution.
static float findValue (juce::AudioProcessorParameter* p, const juce::String& want, bool numeric)
{
    const int N = 40000;
    const double target = textNumber (want);
    double bestErr = 1e30;
    int lo = -1, hi = -1;
    for (int i = 0; i <= N; ++i)
    {
        const auto t = p->getText ((float) i / N, 64);
        double e;
        if (t.trim().equalsIgnoreCase (want.trim())) e = -1.0;
        else if (numeric) e = std::abs (textNumber (t) - target);
        else continue;
        if (e < bestErr - 1e-12) { bestErr = e; lo = hi = i; }
        else if (std::abs (e - bestErr) <= 1e-12 && hi == i - 1) hi = i;
    }
    if (lo < 0) return -1.0f;
    return (float) (0.5 * (lo + hi) / N);
}
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    if (argc < 4) return 2;
    juce::String mode = argv[1];
    if (mode == "ab-check") {
        int failures = 0;
        auto say = [&failures] (bool ok, const juce::String& what) { failures += ok ? 0 : 1; std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); };
        juce::MidiBuffer midi;
        auto run = [&midi] (juce::AudioPluginInstance& q) {
            juce::AudioBuffer<float> b (juce::jmax (q.getTotalNumInputChannels(), q.getTotalNumOutputChannels()), 512);
            for (int i = 0; i < 12; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (10); b.clear(); q.processBlock (b, midi); }
        };
        auto find = [] (juce::AudioPluginInstance& q, const juce::String& name) -> juce::AudioProcessorParameter* {
            for (auto* x : q.getParameters()) if (x->getName (64) == name) return x;
            return nullptr;
        };
        auto p = load (fm, argv[2], argv[3], 48000.0, 512); if (! p) return 1;
        p->enableAllBuses(); p->prepareToPlay (48000.0, 512); run (*p);
        auto* ab = find (*p, "A/B"); auto* threshold = find (*p, "Threshold"); auto* gain = find (*p, "HF Gain"); auto* side = find (*p, "Side LF Gain");
        if (ab == nullptr || threshold == nullptr || gain == nullptr || side == nullptr) { std::printf ("FAIL parameters not found\n"); return 1; }
        // An AU host lists parameters in the order of their hashed IDs, so a position only means something in VST3.
        const bool positionOk = juce::String (argv[2]) != "VST3" || p->getParameters().getLast() == ab;
        say (! ab->isAutomatable() && ab->getCurrentValueAsText() == "A" && positionOk, "the switch isn't automatable, reads " + ab->getCurrentValueAsText() + (juce::String (argv[2]) == "VST3" ? ", and is last" : ""));
        auto write = [&] (juce::AudioProcessorParameter* q, float v) { q->setValueNotifyingHost (v); run (*p); };
        write (threshold, 0.25f); write (gain, 0.75f); write (side, 0.25f);
        const auto ta = threshold->getCurrentValueAsText(), ga = gain->getCurrentValueAsText(), sa = side->getCurrentValueAsText();
        write (ab, 1.0f);
        say (ab->getCurrentValueAsText() == "B" && threshold->getCurrentValueAsText() == ta && gain->getCurrentValueAsText() == ga && side->getCurrentValueAsText() == sa,
             "pressing it switches to B and moves nothing else, because B starts as a copy of A: Threshold " + threshold->getCurrentValueAsText() + ", HF Gain " + gain->getCurrentValueAsText() + ", Side LF Gain " + side->getCurrentValueAsText());
        write (threshold, 0.75f); write (gain, 0.25f); write (side, 0.75f);
        const auto tb = threshold->getCurrentValueAsText(), gb = gain->getCurrentValueAsText(), sb = side->getCurrentValueAsText();
        write (ab, 0.0f);
        say (ab->getCurrentValueAsText() == "A" && threshold->getCurrentValueAsText() == ta && gain->getCurrentValueAsText() == ga && side->getCurrentValueAsText() == sa,
             "pressing it again brings back A: Threshold " + ta + ", HF Gain " + ga + ", Side LF Gain " + sa);
        write (ab, 1.0f);
        say (threshold->getCurrentValueAsText() == tb && gain->getCurrentValueAsText() == gb && side->getCurrentValueAsText() == sb,
             "and B again: Threshold " + tb + ", HF Gain " + gb + ", Side LF Gain " + sb);
        juce::MemoryBlock state; p->getStateInformation (state);
        auto q = load (fm, argv[2], argv[3], 48000.0, 512); if (! q) return 1;
        q->enableAllBuses(); q->prepareToPlay (48000.0, 512);
        q->setStateInformation (state.getData(), (int) state.getSize());
        run (*q);
        auto* qab = find (*q, "A/B"); auto* qth = find (*q, "Threshold");
        say (qab->getCurrentValueAsText() == "B" && qth->getCurrentValueAsText() == tb, "a saved state opens on B, with B's settings: " + qth->getCurrentValueAsText());
        qab->setValueNotifyingHost (0.0f); run (*q);
        say (qab->getCurrentValueAsText() == "A" && qth->getCurrentValueAsText() == ta, "and A is still in it: " + qth->getCurrentValueAsText());
        q->releaseResources(); p->releaseResources();
        return failures == 0 ? 0 : 1;
    }
    if (mode == "nan-check") {
        // The real plug-in: one bad input sample, in each EQ mode, stereo and M/S, compressor out and in. Exit code 0 = all recover.
        int failures = 0;
        for (const auto* eqMode : { "COHERENCE", "WEISS" })
            for (const auto* ms : { "Off", "On" })
                for (const auto* dyn : { "Off", "On" })
                    for (const float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
                    {
                        auto p = load (fm, argv[2], argv[3], 48000.0, 512); if (! p) return 1;
                        p->enableAllBuses(); p->prepareToPlay (48000.0, 512);
                        const auto byName = [&] (const char* n, const char* text) {
                            for (auto* q : p->getParameters())
                                if (q->getName (64) == n) { const auto v = findValue (q, text, false); if (v >= 0.0f) q->setValueNotifyingHost (v); }
                        };
                        byName ("EQ Mode", eqMode); byName ("EQ M/S", ms); byName ("Dyn In", dyn);
                        juce::AudioBuffer<float> b (juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()), 512);
                        juce::MidiBuffer midi; juce::Random r (7);
                        int badOut = 0; float level = 0.0f;
                        const auto block = [&] (bool inject) {
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                            for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, 0.25f * (r.nextFloat() * 2.0f - 1.0f));
                            if (inject) b.setSample (0, 100, bad);
                            p->processBlock (b, midi);
                            for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < 512; ++i) if (! std::isfinite (b.getSample (c, i))) ++badOut;
                            level = b.getRMSLevel (0, 0, 512);
                        };
                        for (int i = 0; i < 40; ++i) block (false);
                        block (true);
                        for (int i = 0; i < 100; ++i) block (false);
                        const bool ok = badOut == 0 && level > 0.01f;
                        failures += ok ? 0 : 1;
                        std::printf ("%s %s, EQ M/S %s, Dyn In %s, one %s sample: %d bad output samples, level afterwards %.3f\n", ok ? "PASS" : "FAIL", eqMode, ms, dyn, std::isnan (bad) ? "NaN" : "Inf", badOut, level);
                        p->releaseResources();
                    }
        return failures == 0 ? 0 : 1;
    }
    if ((mode == "state-save" || mode == "state-load") && argc >= 5) {
        auto p = load (fm, argv[2], argv[3], 48000.0, 512); if (! p) return 1;
        p->enableAllBuses(); p->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()), 512);
        juce::MidiBuffer midi;
        auto run = [&] { for (int i = 0; i < 20; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (10); buffer.clear(); p->processBlock (buffer, midi); } };
        const juce::File file { juce::String (argv[4]) };
        if (mode == "state-save") {
            for (auto* q : p->getParameters())
                if (q->isAutomatable() && q->getName (64) != "Bypass")
                    q->setValueNotifyingHost ((float) ((juce::uint32) q->getName (64).hashCode() % 1000u) / 999.0f);
            run();
            juce::MemoryBlock state; p->getStateInformation (state);
            file.replaceWithData (state.getData(), state.getSize());
        } else {
            juce::MemoryBlock state; file.loadFileAsData (state);
            p->setStateInformation (state.getData(), (int) state.getSize());
            run();
        }
        for (auto* q : p->getParameters())
            std::printf ("%s\t%s\n", q->getName (64).toRawUTF8(), q->getCurrentValueAsText().toRawUTF8());
        p->releaseResources();
        return 0;
    }
    if (mode == "link-walk" && argc >= 5) {
        auto p = load (fm, argv[2], argv[3], 48000.0, 512); if (! p) return 1;
        p->enableAllBuses(); p->prepareToPlay (48000.0, 512);
        const juce::File out { juce::String (argv[4]) };
        out.createDirectory();
        if (argc >= 6) {
            juce::MemoryBlock raw, wrapped; juce::File (juce::String (argv[5])).loadFileAsData (raw);
            juce::XmlElement state ("VST3PluginState");
            state.createNewChildElement ("IComponent")->addTextElement (raw.toBase64Encoding());
            juce::AudioProcessor::copyXmlToBinary (state, wrapped);
            p->setStateInformation (wrapped.getData(), (int) wrapped.getSize());
        }
        juce::AudioBuffer<float> buffer (juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()), 512);
        juce::MidiBuffer midi;
        auto run = [&] (int blocks) {
            for (int i = 0; i < blocks; ++i) {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                buffer.clear(); p->processBlock (buffer, midi);
            }
        };
        auto save = [&] (const juce::String& name) {
            juce::MemoryBlock block; p->getStateInformation (block);
            if (auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize()))
                if (auto* c = xml->getChildByName ("IComponent")) {
                    juce::MemoryBlock raw; raw.fromBase64Encoding (c->getAllSubText());
                    out.getChildFile (name + ".state").replaceWithData (raw.getData(), raw.getSize());
                }
        };
        run (100);
        save ("base");
        auto& params = p->getParameters();
        if (argc >= 7) {
            // A script, one step per line: "set<tab>slot<tab>normalised", "show<tab>slot" or "save<tab>name".
            auto named = [&params] (const juce::String& n) -> juce::AudioProcessorParameter* {
                for (auto* x : params) if (x->getName (64) == n) return x;
                return nullptr;
            };
            juce::StringArray lines; juce::File (juce::String (argv[6])).readLines (lines);
            for (const auto& l : lines) {
                auto t = juce::StringArray::fromTokens (l, "\t", "");
                if (t[0] == "set") { if (auto* q = named (t[1])) { q->setValueNotifyingHost (t[2].getFloatValue()); run (15); } }
                else if (t[0] == "show") { if (auto* q = named (t[1])) std::printf ("%s\t%s\t%.5f\n", t[1].toRawUTF8(), q->getCurrentValueAsText().toRawUTF8(), q->getValue()); }
                else if (t[0] == "save") save (t[1]);
            }
        } else if (argc >= 6) {
            // The slots: not 360 Link's own Bypass, Fader Level, Width and Pan (0-3), nor GroupSense (the last).
            for (int i = 4; i < params.size() - 1; ++i) {
                auto* q = params[i];
                const float before = q->getValue();
                const auto textBefore = q->getCurrentValueAsText();
                q->setValueNotifyingHost (before > 0.5f ? 0.2f : 0.8f);
                run (15);
                std::printf ("%d\t%s\t%s\t%s\n", i, q->getName (64).toRawUTF8(), textBefore.toRawUTF8(), q->getCurrentValueAsText().toRawUTF8());
                save (juce::String (i));
                q->setValueNotifyingHost (before);
                run (15);
            }
        }
        p->releaseResources();
        return 0;
    }
    if (mode == "dump") {
        double sr = argc > 4 ? juce::String (argv[4]).getDoubleValue() : 48000.0;
        auto p = load (fm, argv[2], argv[3], sr, 512); if (! p) return 1;
        p->enableAllBuses(); p->prepareToPlay (sr, 512);
        std::printf ("name: %s  version: %s  latency: %d  tail: %.3f  in/out ch: %d/%d  buses in: %d\n", p->getName().toRawUTF8(),
            p->getPluginDescription().version.toRawUTF8(), p->getLatencySamples(), p->getTailLengthSeconds(),
            p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(), p->getBusCount (true));
        int idx = 0;
        for (auto* q : p->getParameters()) {
            auto def = q->getDefaultValue(); int steps = q->getNumSteps();
            std::printf ("[%d] %s | current %s | label '%s' | default %s | steps %d | discrete %d | auto %d | meta %d\n", idx++, q->getName (64).toRawUTF8(), q->getText (q->getValue(), 64).toRawUTF8(),
                q->getLabel().toRawUTF8(), q->getText (def, 64).toRawUTF8(), steps, (int) q->isDiscrete(), (int) q->isAutomatable(), (int) q->isMetaParameter());
            juce::StringArray seen; juce::String line = "     ";
            int pts = (steps > 1 && steps <= 64) ? steps - 1 : 20;
            for (int i = 0; i <= pts; ++i) { auto t = q->getText ((float) i / pts, 64); if (! seen.contains (t)) { seen.add (t); line << t << " ; "; } }
            std::printf ("%s\n", line.toRawUTF8());
        }
        return 0;
    }
    if (mode == "meter" && argc >= 5) {
        const double sr = 48000.0; const int bs = 512;
        auto p = load (fm, argv[2], argv[3], sr, bs); if (! p) return 1;
        p->enableAllBuses();
        auto params = p->getParameters();
        juce::AudioProcessorParameter* meter = nullptr;
        for (auto* x : params) if (x->getName (64) == juce::String (argv[4])) meter = x;
        if (! meter) { std::printf ("no parameter %s\n", argv[4]); return 1; }
        for (int a = 5; a < argc; ++a) {
            juce::String s = argv[a]; auto name = s.upToFirstOccurrenceOf ("=", false, false), val = s.fromFirstOccurrenceOf ("=", false, false);
            juce::AudioProcessorParameter* q = nullptr; for (auto* x : params) if (x->getName (64) == name) q = x;
            if (! q) { std::printf ("no parameter %s\n", name.toRawUTF8()); return 1; }
            float v = findValue (q, val, val.containsAnyOf ("0123456789"));
            if (v < 0) { std::printf ("no value %s for %s\n", val.toRawUTF8(), name.toRawUTF8()); return 1; }
            q->setValueNotifyingHost (v);
            std::printf ("set %s = %s\n", name.toRawUTF8(), q->getText (v, 64).toRawUTF8());
        }
        std::printf ("meter: %s, automatable %d, meta %d, category %d\n", meter->getName (64).toRawUTF8(), (int) meter->isAutomatable(), (int) meter->isMetaParameter(), (int) meter->getCategory());
        p->prepareToPlay (sr, bs);
        const int nch = std::max (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf (nch, bs); juce::MidiBuffer midi;
        long long pos = 0;
        for (int b = 0; b < (int) (2.0 * sr / bs); ++b) {
            buf.clear();
            for (int i = 0; i < bs; ++i, ++pos) {
                const float x = pos < (long long) sr ? (std::fmod (pos * 1000.0 / sr, 1.0) < 0.5 ? 0.5f : -0.5f) : 0.0f;
                for (int c = 0; c < std::min (2, nch); ++c) buf.setSample (c, i, x);
            }
            p->processBlock (buf, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1); // an AU host reads parameter changes from events
            if (b % 4 == 0) std::printf ("t %7.3f s  value %.5f  text %s\n", pos / sr, meter->getValue(), meter->getText (meter->getValue(), 64).toRawUTF8());
        }
        return 0;
    }
    if (mode == "encoder" && argc >= 7) {
        const double sr = 48000.0; const int bs = 512;
        auto p = load (fm, argv[2], argv[3], sr, bs); if (! p) return 1;
        p->enableAllBuses();
        juce::AudioProcessorParameter* q = nullptr;
        for (auto* x : p->getParameters()) if (x->getName (64) == juce::String (argv[4])) q = x;
        if (! q) { std::printf ("no parameter %s\n", argv[4]); return 1; }
        const float delta = juce::String (argv[5]).getFloatValue();
        const int clicks = juce::String (argv[6]).getIntValue();
        p->prepareToPlay (sr, bs);
        const int nch = std::max (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf (nch, bs); juce::MidiBuffer midi;
        double phase = 0.0;
        auto run = [&] (int blocks)
        {
            double power = 0.0;
            for (int b = 0; b < blocks; ++b) {
                buf.clear();
                for (int i = 0; i < bs; ++i, phase += 1000.0 / sr)
                    for (int c = 0; c < std::min (2, nch); ++c) buf.setSample (c, i, (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * phase)));
                p->processBlock (buf, midi);
                power = 0.0;
                for (int i = 0; i < bs; ++i) power += (double) buf.getSample (0, i) * buf.getSample (0, i);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            }
            return 10.0 * std::log10 (power / bs + 1e-30);
        };
        const double ref = run (60);
        for (int k = 0; k <= clicks; ++k) {
            if (k > 0) q->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, q->getValue() + delta));
            const double level = run (50);
            std::printf ("click %3d  norm %.6f  text %-12s  output %+.3f dB\n", k, q->getValue(), q->getCurrentValueAsText().toRawUTF8(), level - ref);
        }
        return 0;
    }
    if (mode == "focus-check" && argc >= 4) {
        // COHERENCE's focus parameters through a real JUCE VST3 host: selector, forward, sync back, host echoes.
        const double sr = 48000.0; const int bs = 512;
        auto p = load (fm, argv[2], argv[3], sr, bs); if (! p) return 1;
        p->enableAllBuses();
        auto find = [&] (const char* name) { juce::AudioProcessorParameter* q = nullptr; for (auto* x : p->getParameters()) if (x->getName (64) == name) q = x; return q; };
        auto* focus = find ("Focus LF Gain"); auto* mainGain = find ("LF Gain"); auto* sideGain = find ("Side LF Gain");
        auto* ms = find ("EQ M/S"); auto* channel = find ("EQ Channel");
        if (! focus || ! mainGain || ! sideGain || ! ms || ! channel) { std::printf ("parameters missing\n"); return 1; }
        p->prepareToPlay (sr, bs);
        const int nch = std::max (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf (nch, bs); juce::MidiBuffer midi;
        // Two tones through the LF band (a shelf at 80 Hz): in phase is all Mid, out of phase is all Side. The host's own
        // view of the band parameters isn't what counts here; what comes out is.
        double phase = 0.0, midPower = 0.0, sidePower = 0.0;
        auto run = [&] (int blocks) {
            for (int b = 0; b < blocks; ++b) {
                buf.clear();
                for (int i = 0; i < bs; ++i, phase += 1.0 / sr) {
                    const float m = (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 30.0 * phase));
                    const float d = (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 37.0 * phase));
                    buf.setSample (0, i, m + d); if (nch > 1) buf.setSample (1, i, m - d);
                }
                p->processBlock (buf, midi);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            }
        };
        // Level of each tone, by correlating the Mid and Side of the output with it over the last blocks.
        auto levels = [&] {
            juce::AudioBuffer<float> keep (2, bs * 40);
            double start = phase;
            for (int b = 0; b < 40; ++b) {
                buf.clear();
                for (int i = 0; i < bs; ++i, phase += 1.0 / sr) {
                    const float m = (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 30.0 * phase));
                    const float d = (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 37.0 * phase));
                    buf.setSample (0, i, m + d); if (nch > 1) buf.setSample (1, i, m - d);
                }
                p->processBlock (buf, midi);
                keep.copyFrom (0, b * bs, buf, 0, 0, bs); keep.copyFrom (1, b * bs, buf, 1, 0, bs);
            }
            double mc = 0, ms2 = 0, sc = 0, ss = 0;
            for (int i = 0; i < keep.getNumSamples(); ++i) {
                const double t = start + i / sr, mid = 0.5 * (keep.getSample (0, i) + keep.getSample (1, i)), side = 0.5 * (keep.getSample (0, i) - keep.getSample (1, i));
                mc += mid * std::cos (2.0 * juce::MathConstants<double>::pi * 30.0 * t); ms2 += mid * std::sin (2.0 * juce::MathConstants<double>::pi * 30.0 * t);
                sc += side * std::cos (2.0 * juce::MathConstants<double>::pi * 37.0 * t); ss += side * std::sin (2.0 * juce::MathConstants<double>::pi * 37.0 * t);
            }
            const double n = 0.5 * keep.getNumSamples() * 0.05;
            midPower = 20.0 * std::log10 (std::sqrt (mc * mc + ms2 * ms2) / n);
            sidePower = 20.0 * std::log10 (std::sqrt (sc * sc + ss * ss) / n);
        };
        auto show = [&] (const char* what) {
            levels();
            std::printf ("%-42s focus reads %-10s  Mid tone %+6.2f dB  Side tone %+6.2f dB\n", what, focus->getCurrentValueAsText().toRawUTF8(), midPower, sidePower);
        };
        const float stepNorm = 0.5f / 30.0f;   // 0.5 dB on the +-15 dB range
        run (20);
        show ("start");
        for (int k = 0; k < 4; ++k) { focus->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, focus->getValue() + stepNorm)); run (10); }
        run (40);
        show ("stereo: 4 clicks up on the focus");
        ms->setValueNotifyingHost (1.0f); channel->setValueNotifyingHost (1.0f); run (40);
        show ("M/S, selector on Side");
        for (int k = 0; k < 6; ++k) { focus->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, focus->getValue() - stepNorm)); run (10); }
        run (40);
        show ("Side: 6 clicks down on the focus");
        channel->setValueNotifyingHost (0.0f); run (40);
        show ("selector on Mid");
        for (int k = 0; k < 40; ++k) { focus->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, focus->getValue() + stepNorm)); run (1); }
        run (40);
        show ("Mid: fast spin, 40 clicks up");
        mainGain->setValueNotifyingHost (0.5f); run (40);
        show ("the band's own parameter set to centre");
        const float a = mainGain->getValue(), b = focus->getValue();
        run (100);
        std::printf ("after 100 more blocks, settled: main moved %.6f, focus moved %.6f\n", mainGain->getValue() - a, focus->getValue() - b);
        return 0;
    }
    if (mode == "render" && argc >= 6) {
        juce::AudioFormatManager afm; afm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (afm.createReaderFor (juce::File (argv[4])));
        if (! r) { std::printf ("cannot read input\n"); return 1; }
        double sr = r->sampleRate; int n = (int) r->lengthInSamples; const int bs = 512;
        auto p = load (fm, argv[2], argv[3], sr, bs); if (! p) return 1;
        p->enableAllBuses();
        auto params = p->getParameters();
        for (int a = 6; a < argc; ++a) {
            juce::String s = argv[a]; auto name = s.upToFirstOccurrenceOf ("=", false, false), val = s.fromFirstOccurrenceOf ("=", false, false);
            juce::AudioProcessorParameter* q = nullptr; for (auto* x : params) if (x->getName (64) == name) q = x;
            if (! q) { std::printf ("no parameter %s\n", name.toRawUTF8()); return 1; }
            float v = findValue (q, val, val.containsAnyOf ("0123456789"));
            if (v < 0) { std::printf ("no value %s for %s\n", val.toRawUTF8(), name.toRawUTF8()); return 1; }
            q->setValueNotifyingHost (v);
            std::printf ("set %s = %s (norm %.6f)\n", name.toRawUTF8(), q->getText (v, 64).toRawUTF8(), v);
        }
        p->prepareToPlay (sr, bs);
        int nch = std::max (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
        int lat = p->getLatencySamples();
        std::printf ("latency %d samples\n", lat);
        juce::AudioBuffer<float> in (2, n), out (2, n + lat); r->read (&in, 0, n, 0, true, true);
        if (r->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, n);
        juce::AudioBuffer<float> buf (nch, bs); juce::MidiBuffer midi;
        for (int pos = 0; pos < n + lat; pos += bs) {
            int len = std::min (bs, n + lat - pos); buf.clear();
            for (int c = 0; c < 2; ++c) if (pos < n) buf.copyFrom (c, 0, in, c, pos, std::min (len, n - pos));
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), nch, len);
            p->processBlock (view, midi);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, len);
        }
        juce::AudioBuffer<float> trimmed (2, n); for (int c = 0; c < 2; ++c) trimmed.copyFrom (c, 0, out, c, lat, n);
        juce::File of (argv[5]); of.deleteFile();
        juce::WavAudioFormat wav; auto os = of.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.release(), sr, 2, 32, {}, 0));
        w->writeFromAudioSampleBuffer (trimmed, 0, n);
        std::printf ("latency after processing: %d\n", p->getLatencySamples());
        if (auto* env = std::getenv ("PROBE_TEXTS")) for (auto* x : params) if (x->getName (64) == juce::String (env)) {
            juce::StringArray seen; for (int i = 0; i <= 40; ++i) { auto t = x->getText (i / 40.f, 64); if (! seen.contains (t)) seen.add (t); }
            std::printf ("%s range: %s\n", env, seen.joinIntoString (" ; ").toRawUTF8()); }
        std::printf ("wrote %s (latency removed)\n", argv[5]);
        return 0;
    }
    return 2;
}
