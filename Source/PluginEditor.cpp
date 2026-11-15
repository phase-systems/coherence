#include "PluginEditor.h"

namespace coherence
{
using namespace theme;
using gui::ConsoleButton;
using gui::Knob;

using M = std::initializer_list<std::pair<float, const char*>>;

//==============================================================================
// Everything on the panel that doesn't move: surfaces, printing, the plate. Cached, because it never changes while the
// meters run; a repaint (a theme change, say) rebuilds it.
class CoherenceEditor::Panel final : public juce::Component
{
public:
    explicit Panel (CoherenceProcessor& pr) : proc (pr)
    {
        setInterceptsMouseClicks (false, false);
        setBufferedToImage (true);
    }

    void paint (juce::Graphics& g) override
    {
        using RF = juce::Rectangle<float>;
        const auto& t = current();
        // A little past the window's bottom edge: at most sizes the panel is a fraction of a pixel taller than the window,
        // and the last row must be fully painted.
        const float W = (float) panelWidth, H = (float) panelHeight + 2.0f, mainH = 634.0f;

        for (float x : { 0.0f, 540.0f })
        {
            juce::ColourGradient bg (t.panelTop, 0.0f, 0.0f, t.panel, 0.0f, 300.0f, false);
            g.setGradientFill (bg);
            g.fillRect (RF (x, 0.0f, 180.0f, mainH));
        }
        g.setColour (t.meterColumn);
        g.fillRect (RF (180.0f, 39.0f, 360.0f, mainH - 39.0f));
        {
            juce::ColourGradient h (t.header.brighter (0.08f), 0.0f, 0.0f, t.header.darker (0.12f), 0.0f, 39.0f, false);
            g.setGradientFill (h);
            g.fillRect (RF (180.0f, 0.0f, 360.0f, 39.0f));
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (RF (180.0f, 38.5f, 360.0f, 1.0f));
        g.setColour (t.block);
        g.fillRect (RF (270.0f, 39.0f, 180.0f, 167.0f));
        {
            juce::ColourGradient s (t.stripTop, 0.0f, 207.0f, t.stripBottom, 0.0f, mainH, false);
            g.setGradientFill (s);
            g.fillRect (RF (270.0f, 207.0f, 180.0f, mainH - 207.0f));
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (RF (270.0f, 206.0f, 180.0f, 1.2f));
        for (float x : { 180.0f, 270.0f, 450.0f, 540.0f })
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (RF (x - 0.5f, 39.0f, 1.0f, mainH - 39.0f));
        }
        drawGrain (g, RF (0.0f, 0.0f, W, mainH));

        // ---- EQ printing
        const auto bandLabel = [&] (const juce::String& s, float x, float y) { text (g, s, x, y, sans (10.6f), t.ink, juce::Justification::centredLeft); };
        text (g, "SHAPE", 5.0f, 123.0f, sans (13.2f), t.ink, juce::Justification::centredLeft);
        drawRule (g, { { 4.0f, 135.0f }, { 176.0f, 135.0f } });
        bandLabel ("HF", 6.0f, 212.0f);
        drawRule (g, { { 7.0f, 223.5f }, { 72.0f, 223.5f }, { 102.0f, 254.0f }, { 173.0f, 254.0f } });
        bandLabel ("HMF", 7.0f, 304.0f);
        text (g, "EQ", 5.0f, 380.0f, sans (15.5f), t.ink, juce::Justification::centredLeft);
        drawRule (g, { { 31.0f, 380.0f }, { 88.0f, 380.0f } });
        bandLabel ("LMF", 6.0f, 464.0f);
        drawRule (g, { { 7.0f, 542.0f }, { 72.0f, 542.0f }, { 102.0f, 511.5f }, { 173.0f, 511.5f } });
        bandLabel ("LF", 6.0f, 561.0f);

        // ---- Meter printing
        for (const bool input : { true, false })
        {
            const float colX = input ? 180.0f : 450.0f;
            text (g, input ? "INPUT" : "OUTPUT", colX + 45.0f + (input ? 17.0f : -17.0f), 60.5f, sans (10.6f), juce::Colour (0xff8a8b8e));
            const float nx = input ? colX + 23.5f : colX + 68.7f;
            for (int d = 0; d <= 60; d += 6)
                text (g, juce::String (d), nx, gui::LevelMeter::yOfDb ((float) -d), sans (9.3f, true), d <= 12 ? t.scaleHot : t.scaleCool,
                      input ? juce::Justification::centredRight : juce::Justification::centredLeft);
        }
        text (g, "EQ OUTPUT", 325.0f, 587.5f, sans (8.9f), t.stripInk);

        // ---- Compressor printing
        text (g, "COMPRESS", 547.0f, 19.0f, sans (11.2f), t.ink, juce::Justification::centredLeft);
        text (g, "MIX", 702.0f, 19.0f, sans (11.2f), t.ink);
        text (g, "AUTO RELEASE", 679.0f, 49.0f, sans (8.6f), t.ink, juce::Justification::centredRight);
        text (g, "AUTO MAKEUP", 679.0f, 71.0f, sans (8.6f), t.ink, juce::Justification::centredRight);
        text (g, "DYN", 549.0f, 253.0f, sans (15.5f), t.ink, juce::Justification::centredLeft);
        drawRule (g, { { 610.0f, 253.0f }, { 640.0f, 253.0f } });
        {
            juce::Path b;
            b.startNewSubPath (646.0f, 208.5f);
            b.lineTo (640.0f, 208.5f);
            b.lineTo (640.0f, 297.5f);
            b.lineTo (646.0f, 297.5f);
            g.setColour (t.rule);
            g.strokePath (b.createPathWithRoundedCorners (3.0f), juce::PathStrokeType (1.0f));
        }

        // ---- Phase Systems plate
        {
            const RF band (540.0f, 469.0f, 180.0f, 26.0f);
            juce::ColourGradient s (t.plateTop, 0.0f, band.getY(), t.plateBottom, 0.0f, band.getBottom(), false);
            g.setGradientFill (s);
            g.fillRect (band);
            const auto plate = band.reduced (4.0f, 3.5f);
            g.setColour (juce::Colour (0xff2b2b2d));
            g.drawRoundedRectangle (plate, 3.0f, 1.0f);
            drawMark (g, plate.getX() + 7.0f, plate.getCentreY(), 15.0f, 3.0f);
            text (g, juce::String::fromUTF8 ("PHASE SYSTEMS \xe2\x80\xa2 COHERENCE"), plate.getCentreX() + 8.0f, plate.getCentreY() + 0.2f, sans (9.6f, false, 0.02f), t.plateInk);
        }

        // ---- Bottom bar
        {
            juce::ColourGradient b (t.barTop, 0.0f, mainH, t.barBottom, 0.0f, H, false);
            g.setGradientFill (b);
            g.fillRect (RF (0.0f, mainH, W, H - mainH));
            g.setColour (juce::Colours::black);
            g.fillRect (RF (0.0f, mainH, W, 1.0f));
            for (float x : { 180.0f, 540.0f })
            {
                g.setColour (juce::Colour (0xff05060a));
                g.fillRect (RF (x - 0.5f, mainH, 1.0f, H - mainH));
            }
            text (g, "v" JucePlugin_VersionString, 561.0f, 651.0f, sans (9.2f), t.barDim);
            const double sr = proc.currentSampleRate();
            if (sr > 0.0)
            {
                const double khz = sr / 1000.0;
                text (g, juce::String (khz, std::abs (khz - std::round (khz)) < 0.05 ? 0 : 1) + " kHz   " + juce::String (proc.getLatencySamples()) + " smp",
                      708.0f, 651.0f, sans (9.2f), t.barDim, juce::Justification::centredRight);
            }
        }
    }

private:
    CoherenceProcessor& proc;
};

//==============================================================================
// The header's readout: the product, the channel the EQ controls are on, and the EQ mode.
class CoherenceEditor::HeaderDisplay final : public juce::Component
{
public:
    explicit HeaderDisplay (CoherenceProcessor& pr) : proc (pr) { setInterceptsMouseClicks (false, false); }

    void setFace (juce::Rectangle<float> f)
    {
        const auto b = f.expanded (2.0f).getSmallestIntegerContainer();
        setBounds (b);
        face = f.translated ((float) -b.getX(), (float) -b.getY());
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = current();
        const auto d = face;
        lcdPanel (g, d);
        auto& s = proc.state();
        const bool bypassed = s.getRawParameterValue (id::bypass)->load() >= 0.5f;
        lcdText (g, "COHERENCE", d.getX() + 8.0f, d.getCentreY() + 0.2f, 1.42f, t.lcdLit);
        if (bypassed)
            lcdText (g, "BYPASSED", d.getCentreX() + 2.0f, d.getCentreY() + 0.2f, 1.1f, t.lcdAlert);
        else if (s.getRawParameterValue (id::eqMs)->load() >= 0.5f)
        {
            const bool side = s.getRawParameterValue (id::eqChannel)->load() >= 0.5f;
            lcdText (g, side ? "SIDE" : "MID", d.getCentreX() + 2.0f, d.getCentreY() + 0.2f, 1.1f, side ? t.lcdAlert : t.lcdLit);
        }
        const juce::String mode = s.getRawParameterValue (id::eqMode)->load() >= 0.5f ? "WEISS" : "ZERO LATENCY";
        lcdText (g, mode, d.getRight() - lcdWidth (mode, 1.1f) - 8.0f, d.getCentreY() + 0.2f, 1.1f, t.lcdDim);
    }

private:
    CoherenceProcessor& proc;
    juce::Rectangle<float> face;
};

//==============================================================================
// The bottom bar's readout: what is under the mouse, and its value.
class CoherenceEditor::StatusDisplay final : public juce::Component
{
public:
    StatusDisplay() { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& s)
    {
        const auto next = lcdString (s);
        if (next != shown)
        {
            shown = next;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = current();
        const float cy = (float) getHeight() * 0.5f, cx = (float) getWidth() * 0.5f;
        if (shown.isEmpty())
        {
            const juce::String idle ("PHASE SYSTEMS");
            lcdText (g, idle, cx - lcdWidth (idle, 1.05f) * 0.5f, cy, 1.05f, t.lcdDim.withAlpha (0.45f));
            return;
        }
        const float p = juce::jmin (1.3f, ((float) getWidth() - 16.0f) / (6.0f * (float) shown.length()));
        lcdText (g, shown, cx - lcdWidth (shown, p) * 0.5f, cy, p, t.lcdLit);
    }

private:
    juce::String shown;
};

//==============================================================================
class CoherenceEditor::Content final : public juce::Component
{
public:
    Content (CoherenceEditor& e, CoherenceProcessor& pr) : ed (e), proc (pr)
    {
        auto* um = &proc.undo();
        auto& s = proc.state();
        auto P = [&s] (const juce::String& pid) -> juce::RangedAudioParameter& { return *s.getParameter (pid); };
        const auto marks = [] (juce::RangedAudioParameter& q, M m)
        {
            std::vector<Mark> v;
            for (const auto& [x, l] : m)
                v.push_back ({ q.convertTo0to1 (x), juce::String::fromUTF8 (l) });
            return v;
        };

        panel = std::make_unique<Panel> (proc);
        addAndMakeVisible (*panel);

        // EQ: index 0..3 = LF, LMF, HMF, HF (parameter order). Two sets of controls in the same places, one shown at a
        // time: the main EQ (stereo, or Mid in M/S) and the Side channel.
        const M gainScale { { -15.f, "-15" }, { -12.f, "12" }, { -9.f, "9" }, { -6.f, "6" }, { -3.f, "3" }, { 0.f, "0" }, { 3.f, "3" }, { 6.f, "6" }, { 9.f, "9" }, { 12.f, "12" }, { 15.f, "+15" } };
        const M qScale { { .3f, ".3" }, { .5f, ".5" }, { 1.f, "1" }, { 2.f, "2" }, { 4.f, "4" }, { 10.f, "10" } };
        const M freqScale[4] = { { { 16.f, "16" }, { 30.f, "30" }, { 60.f, "60" }, { 120.f, "120" }, { 250.f, "250" }, { 500.f, "500" }, { 1000.f, "1k" } },
                                 { { 40.f, "40" }, { 100.f, "100" }, { 200.f, "200" }, { 500.f, "500" }, { 1000.f, "1k" }, { 2000.f, "2k" }, { 4000.f, "4k" } },
                                 { { 300.f, ".3" }, { 500.f, ".5" }, { 1000.f, "1" }, { 2000.f, "2" }, { 4000.f, "4" }, { 8000.f, "8" }, { 16000.f, "16" } },
                                 { { 1000.f, "1" }, { 1500.f, "1.5" }, { 2000.f, "2" }, { 3000.f, "" }, { 5000.f, "5" }, { 8000.f, "8" }, { 12000.f, "12" }, { 16000.f, "" }, { 20000.f, "20" } } };
        const Cap caps[4] = { Cap::lf, Cap::lmf, Cap::hmf, Cap::hf };
        const char* const freqUnit[4] = { "Hz", "Hz", "kHz", "kHz" };
        const auto makeBand = [&] (Band& band, const juce::String& b, int i)
        {
            const bool hasType = i == 0 || i == 3;
            auto& gain = P (id::bandGain (b));
            auto& freq = P (id::bandFreq (b));
            auto& q = P (id::bandQ (b));
            band.gain = std::make_unique<Knob> (gain, um, "dB", KnobLook { caps[i], marks (gain, gainScale) }, &ed);
            band.freq = std::make_unique<Knob> (freq, um, freqUnit[i], KnobLook { caps[i], marks (freq, freqScale[i]) }, &ed);
            band.q = std::make_unique<Knob> (q, um, hasType ? (i == 3 ? "HF Q" : "LF Q") : "Q", KnobLook { hasType ? Cap::grey : caps[i], marks (q, qScale) }, &ed);
            addAndMakeVisible (*band.gain);
            addAndMakeVisible (*band.freq);
            addAndMakeVisible (*band.q);
            if (hasType)
            {
                band.type = std::make_unique<gui::CycleKey> (P (id::bandType (b)), um, juce::StringArray { "BELL", "SHELF", "CUT" }, &ed);
                band.type->onChange = [this] { ed.updateBandStates(); };
                addAndMakeVisible (*band.type);
            }
        };
        for (int i = 0; i < 4; ++i)
        {
            const juce::String b (id::bands[i]);
            makeBand (bands[(size_t) i], b, i);
            makeBand (sideBands[(size_t) i], id::sideBand (b), i);
        }
        eqIn = std::make_unique<ConsoleButton> (P (id::eqOn), um, "IN", &ed);
        eqIn->setTextHeight (9.4f);
        addAndMakeVisible (*eqIn);
        eqMs = std::make_unique<ConsoleButton> (P (id::eqMs), um, "M/S", &ed);
        eqMs->setTextHeight (7.6f);
        addAndMakeVisible (*eqMs);
        eqChannel = std::make_unique<gui::Segmented> (P (id::eqChannel), um, juce::StringArray { "MID", "SIDE" }, &ed);
        eqChannel->onChange = [this] { ed.updateEqChannel(); };
        addAndMakeVisible (*eqChannel);

        // Header
        power = std::make_unique<ConsoleButton> (P (id::bypass), um, juce::String(), &ed);
        power->setStyle (ConsoleButton::Style::powerKey);
        addAndMakeVisible (*power);
        header = std::make_unique<HeaderDisplay> (proc);
        addAndMakeVisible (*header);
        sizeButton = std::make_unique<ConsoleButton> ("SETUP", [this] { ed.showSetupMenu(); }, nullptr, "SETUP: EQ MODE, THEME, WINDOW SIZE, STEPPING", &ed);
        sizeButton->setTextHeight (8.2f);
        addAndMakeVisible (*sizeButton);

        // Centre
        const M trimScale { { -24.f, "-24" }, { -18.f, "" }, { -12.f, "12" }, { -6.f, "" }, { 0.f, "0" }, { 6.f, "" }, { 12.f, "12" }, { 18.f, "" }, { 24.f, "+24" } };
        const M levelScale { { -12.f, "-12" }, { -6.f, "6" }, { 0.f, "0" }, { 6.f, "6" }, { 12.f, "+12" }, { -9.f, "" }, { -3.f, "" }, { 3.f, "" }, { 9.f, "" } };
        const M balanceScale { { -100.f, "L" }, { -60.f, "" }, { -20.f, "" }, { 0.f, "C" }, { 20.f, "" }, { 60.f, "" }, { 100.f, "R" } };
        input = std::make_unique<Knob> (P (id::input), um, "IN TRIM", KnobLook { Cap::grey, marks (P (id::input), trimScale) }, &ed);
        balance = std::make_unique<Knob> (P (id::balance), um, "BALANCE", KnobLook { Cap::grey, marks (P (id::balance), balanceScale) }, &ed);
        addAndMakeVisible (*input);
        addAndMakeVisible (*balance);
        mono = std::make_unique<ConsoleButton> (P (id::mono), um, "MONO", &ed);
        mono->setTextHeight (11.0f);
        addAndMakeVisible (*mono);

        mid = std::make_unique<Knob> (P (id::mid), um, "MID", KnobLook { Cap::level, marks (P (id::mid), levelScale), true, true, 35.0f }, &ed);
        side = std::make_unique<Knob> (P (id::side), um, "SIDE", KnobLook { Cap::level, marks (P (id::side), levelScale), true, true, 35.0f }, &ed);
        addAndMakeVisible (*mid);
        addAndMakeVisible (*side);
        link = std::make_unique<ConsoleButton> (P (id::msLink), um, "LINK", &ed);
        link->setTextHeight (7.4f);
        addAndMakeVisible (*link);
        setUpLink (*mid, *side);
        // dB controls move in 0.1 dB steps, 0.01 dB with Shift (or Cmd).
        setUpLink (*side, *mid);

        msMeter = std::make_unique<gui::MidSideMeter> (&ed);
        addAndMakeVisible (*msMeter);
        fader = std::make_unique<gui::Fader> (P (id::output), um, &ed);
        addAndMakeVisible (*fader);
        eqOutput = std::make_unique<Knob> (P (id::eqOutput), um, "EQ OUTPUT", KnobLook {}, &ed);
        addAndMakeVisible (*eqOutput);

        inMeter = std::make_unique<gui::LevelMeter> ("INPUT", gui::LevelMeter::ScaleSide::left, &ed);
        outMeter = std::make_unique<gui::LevelMeter> ("OUTPUT", gui::LevelMeter::ScaleSide::right, &ed);
        addAndMakeVisible (*inMeter);
        addAndMakeVisible (*outMeter);

        // Compressor: one section. Left: ratio, timing and sidechain. Right: threshold, knee and level. The controls sit
        // where the existing UC1 mappings put them.
        const float kneePos[] = { 0.f, 1.f, 4.f, 8.f, 20.f, 40.f, 72.f };
        const char* const kneeText[] = { "0", "1", "4", "8", "20", "40", "72" };
        std::vector<Mark> kneeMarks;
        for (int i = 0; i < 7; ++i)
            kneeMarks.push_back ({ std::pow (kneePos[i] / 72.0f, 1.0f / 2.4f), kneeText[i] });   // the stored value is 0..1, so this is its position
        mix = std::make_unique<Knob> (P (id::mix), um, "MIX", KnobLook {}, &ed);
        addAndMakeVisible (*mix);
        ratio = std::make_unique<Knob> (P (id::ratio), um, "RATIO", KnobLook { Cap::grey, marks (P (id::ratio), { { 1.f, "1" }, { 1.5f, "" }, { 2.f, "2" }, { 3.f, "" }, { 4.f, "4" }, { 8.f, "8" }, { 20.f, "20" }, { 1000.f, "\xe2\x88\x9e" } }) }, &ed);
        threshold = std::make_unique<Knob> (P (id::threshold), um, "THRESHOLD", KnobLook { Cap::grey, marks (P (id::threshold), { { -60.f, "-60" }, { -50.f, "" }, { -40.f, "40" }, { -30.f, "30" }, { -20.f, "20" }, { -10.f, "10" }, { 0.f, "0" } }) }, &ed);
        attack = std::make_unique<Knob> (P (id::attack), um, "ATTACK", KnobLook { Cap::grey, marks (P (id::attack), { { .05f, ".05" }, { .2f, ".2" }, { 1.f, "1" }, { 5.f, "5" }, { 20.f, "20" }, { 150.f, "150" } }) }, &ed);
        release = std::make_unique<Knob> (P (id::release), um, "RELEASE", KnobLook { Cap::dark, marks (P (id::release), { { 5.f, "5" }, { 20.f, "20" }, { 50.f, "50" }, { 150.f, "150" }, { 500.f, "500" }, { 3000.f, "3k" } }) }, &ed);
        knee = std::make_unique<Knob> (P (id::knee), um, "KNEE", KnobLook { Cap::dark, kneeMarks }, &ed);
        scHpf = std::make_unique<Knob> (P (id::scHpf), um, "SC HPF", KnobLook { Cap::dark, marks (P (id::scHpf), { { 20.f, "OFF" }, { 30.f, "30" }, { 50.f, "50" }, { 100.f, "100" }, { 200.f, "200" }, { 500.f, "500" } }) }, &ed);
        makeup = std::make_unique<Knob> (P (id::makeup), um, "MAKEUP", KnobLook { Cap::dark, marks (P (id::makeup), trimScale) }, &ed);
        for (auto* k : { threshold.get(), ratio.get(), attack.get(), release.get(), knee.get(), makeup.get(), scHpf.get() })
            addAndMakeVisible (*k);

        autoRelease = std::make_unique<ConsoleButton> (P (id::autoRelease), um, juce::String(), &ed);
        autoMakeup = std::make_unique<ConsoleButton> (P (id::autoMakeup), um, juce::String(), &ed);
        dynIn = std::make_unique<ConsoleButton> (P (id::dynOn), um, "IN", &ed);
        dynIn->setTextHeight (9.4f);
        for (auto* b : { autoRelease.get(), autoMakeup.get(), dynIn.get() })
            addAndMakeVisible (*b);
        grMeter = std::make_unique<gui::GainReductionMeter> (&ed);
        addAndMakeVisible (*grMeter);

        routing = std::make_unique<gui::RoutingDisplay> (P (id::order), um, &ed);
        routing->eqActive = [&s] { return s.getRawParameterValue (id::eqOn)->load() >= 0.5f; };
        routing->dynActive = [&s] { return s.getRawParameterValue (id::dynOn)->load() >= 0.5f; };
        addAndMakeVisible (*routing);

        // Bottom bar
        undoButton = std::make_unique<ConsoleButton> ("UNDO", [this] { proc.undoStep (false); }, nullptr, "UNDO", &ed);
        undoButton->setStyle (ConsoleButton::Style::undoIcon);
        redoButton = std::make_unique<ConsoleButton> ("REDO", [this] { proc.undoStep (true); }, nullptr, "REDO", &ed);
        redoButton->setStyle (ConsoleButton::Style::redoIcon);
        // Either letter switches to the other slot. The lit one is the slot in use.
        aButton = std::make_unique<ConsoleButton> ("A", [this] { proc.toggleAbSlot(); }, [this] { return proc.getAbSlot() == 0; }, "COMPARE: SWITCH BETWEEN A AND B", &ed);
        bButton = std::make_unique<ConsoleButton> ("B", [this] { proc.toggleAbSlot(); }, [this] { return proc.getAbSlot() == 1; }, "COMPARE: SWITCH BETWEEN A AND B", &ed);
        aButton->setStyle (ConsoleButton::Style::letter);
        bButton->setStyle (ConsoleButton::Style::letter);
        copyButton = std::make_unique<ConsoleButton> ("COPY", [this] { proc.copyCurrentToOtherSlot(); }, nullptr, "COPY THESE SETTINGS TO THE OTHER SLOT", &ed);
        copyButton->setStyle (ConsoleButton::Style::plainText);
        for (auto* b : { undoButton.get(), redoButton.get(), aButton.get(), bButton.get(), copyButton.get() })
            addAndMakeVisible (*b);
        status = std::make_unique<StatusDisplay>();
        addAndMakeVisible (*status);

        doLayout();
    }

    void setUpLink (Knob& self, Knob& other)
    {
        auto& s = proc.state();
        self.onGestureStart = [&s, &other] { if (s.getRawParameterValue (id::msLink)->load() >= 0.5f) other.beginExternalGesture(); };
        self.onGestureEnd = [&s, &other] { if (s.getRawParameterValue (id::msLink)->load() >= 0.5f) other.endExternalGesture(); };
        self.onUserChange = [&s, &other] (float now, float before)
        {
            if (s.getRawParameterValue (id::msLink)->load() < 0.5f)
                return;
            auto& op = other.parameter();
            const auto range = op.getNormalisableRange();
            const float cur = op.convertFrom0to1 (op.getValue());
            other.setValueFromLink (juce::jlimit (range.start, range.end, cur + (now - before)));
        };
    }

    static void place (Knob& k, int cx, int cy)
    {
        k.setBounds (cx - (int) Knob::centreX, cy - (int) Knob::centreY, Knob::boundsW, Knob::boundsH);
    }

    // Panel coordinates. They are the same for every theme, because a theme is only colours.
    void doLayout()
    {
        using RF = juce::Rectangle<float>;
        panel->setBounds (0, 0, panelWidth, panelHeight + 2);

        // EQ. Parameter index: 0 LF, 1 LMF, 2 HMF, 3 HF. The zig-zag is the 4K's, and the UC1's.
        for (auto* set : { &bands, &sideBands })
        {
            auto& lf = (*set)[0]; auto& lmf = (*set)[1]; auto& hmf = (*set)[2]; auto& hf = (*set)[3];
            place (*hf.q, 49, 45);
            place (*lf.q, 129, 81);
            place (*hf.gain, 49, 170);
            hf.type->setFace (RF (110.0f, 146.5f, 38.0f, 22.0f));
            place (*hf.freq, 129, 211);
            place (*hmf.gain, 49, 260);
            place (*hmf.freq, 129, 298);
            place (*hmf.q, 49, 334);
            place (*lmf.gain, 49, 421);
            place (*lmf.freq, 129, 458);
            place (*lmf.q, 49, 495);
            place (*lf.freq, 129, 548);
            place (*lf.gain, 49, 586);
            lf.type->setFace (RF (110.0f, 595.5f, 38.0f, 22.0f));
        }
        eqIn->setFace (RF (93.0f, 364.0f, 21.0f, 36.0f));
        eqMs->setFace (RF (120.0f, 364.0f, 22.0f, 36.0f));
        eqChannel->setFace (RF (148.0f, 361.0f, 21.0f, 42.0f));

        // Header
        power->setFace (RF (197.0f, 9.0f, 36.0f, 20.0f));
        header->setFace (RF (248.0f, 8.5f, 223.0f, 21.0f));
        sizeButton->setFace (RF (487.0f, 7.0f, 36.0f, 21.0f));

        // Meters
        const int meterW = 49, meterH = juce::roundToInt (gui::LevelMeter::bottom - gui::LevelMeter::top), meterTop = (int) gui::LevelMeter::top;
        inMeter->setBounds (210, meterTop, meterW, meterH);
        outMeter->setBounds (464, meterTop, meterW, meterH);

        // Centre
        place (*input, 325, 86);
        place (*balance, 395, 86);
        mono->setFace (RF (331.0f, 148.0f, 58.0f, 30.0f));
        place (*mid, 325, 247);
        place (*side, 395, 247);
        link->setFace (RF (346.0f, 277.0f, 28.0f, 16.0f));
        msMeter->setBounds (300, 304, 50, 268);
        fader->setSlot (395.0f, 303.0f, 612.0f);
        eqOutput->setReadout (RF (299.0f, 595.0f, 52.0f, 24.0f), 1.08f);

        // Compressor
        mix->setReadout (RF (618.0f, 8.5f, 62.0f, 21.0f), 1.25f, [] (float v) { return juce::String (v, 1) + "%"; });
        autoRelease->setFace (RF (685.0f, 41.5f, 21.0f, 15.0f));
        autoMakeup->setFace (RF (685.0f, 63.5f, 21.0f, 15.0f));
        place (*ratio, 591, 86);
        place (*threshold, 668, 128);
        place (*attack, 591, 180);
        dynIn->setFace (RF (583.0f, 235.0f, 21.0f, 36.0f));
        grMeter->setBounds (634, 197, 58, 112);
        place (*release, 591, 316);
        place (*knee, 668, 344);
        place (*scHpf, 591, 396);
        place (*makeup, 668, 424);
        routing->setBounds (RF (550.0f, 504.0f, 160.0f, 121.0f).expanded (2.0f).toNearestInt());

        // Bottom bar
        undoButton->setFace (RF (9.0f, 640.0f, 24.0f, 21.0f));
        redoButton->setFace (RF (40.0f, 640.0f, 24.0f, 21.0f));
        copyButton->setFace (RF (72.0f, 642.0f, 36.0f, 18.0f));
        aButton->setFace (RF (122.0f, 639.5f, 18.0f, 22.0f));
        bButton->setFace (RF (154.0f, 639.5f, 18.0f, 22.0f));
        status->setBounds (181, 635, 358, 31);
    }

    void repaintEverything()
    {
        panel->repaint();
        repaint();
    }

    CoherenceEditor& ed;
    CoherenceProcessor& proc;

    struct Band
    {
        std::unique_ptr<Knob> gain, freq, q;
        std::unique_ptr<gui::CycleKey> type;
    };
    std::unique_ptr<Panel> panel;
    std::array<Band, 4> bands, sideBands;
    std::unique_ptr<gui::Segmented> eqChannel;
    std::unique_ptr<ConsoleButton> eqMs;
    std::unique_ptr<ConsoleButton> eqIn, dynIn, autoRelease, autoMakeup, link, mono, sizeButton, power;
    std::unique_ptr<ConsoleButton> undoButton, redoButton, aButton, bButton, copyButton;
    std::unique_ptr<HeaderDisplay> header;
    std::unique_ptr<StatusDisplay> status;
    std::unique_ptr<Knob> input, mid, side, balance, eqOutput, threshold, ratio, attack, release, knee, makeup, scHpf, mix;
    std::unique_ptr<gui::Fader> fader;
    std::unique_ptr<gui::LevelMeter> inMeter, outMeter;
    std::unique_ptr<gui::GainReductionMeter> grMeter;
    std::unique_ptr<gui::MidSideMeter> msMeter;
    std::unique_ptr<gui::RoutingDisplay> routing;
};

//==============================================================================
namespace
{
// Only what the panel draws through JUCE itself: the resize corner (quiet, in the bottom bar's ink) and the
// SETUP menu (JUCE's dark menu, which matches the header).
class PanelLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging) override
    {
        const float size = (float) juce::jmin (w, h);
        g.setColour (juce::Colour (0xffa3a19a).withAlpha (over || dragging ? 0.7f : 0.3f));
        for (float i = 0.3f; i < 1.0f; i += 0.3f)
            g.drawLine ((float) w - size * i, (float) h - 1.5f, (float) w - 1.5f, (float) h - size * i, 1.0f);
    }
};
} // namespace

CoherenceEditor::CoherenceEditor (CoherenceProcessor& pr) : AudioProcessorEditor (pr), proc (pr)
{
    lookAndFeel = std::make_unique<PanelLookAndFeel>();
    setLookAndFeel (lookAndFeel.get());
    theme::setCurrent (settings->themeId());
    content = std::make_unique<Content> (*this, proc);
    content->setSize (panelWidth, panelHeight + 2);
    addAndMakeVisible (*content);

    // Free resizing (host window or corner), aspect ratio locked; the panel scales as a whole.
    constrainer.setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    constrainer.setSizeLimits (juce::roundToInt (baseWidth * minScale), juce::roundToInt (baseHeight * minScale),
                               juce::roundToInt (baseWidth * maxScale), juce::roundToInt (baseHeight * maxScale));
    setConstrainer (&constrainer);
    setResizable (true, true);

    // This instance's size if the set saved one, otherwise the size last chosen in any instance.
    const float saved = proc.getUiScale();
    setScale (saved > 0.0f ? saved : settings->defaultScale());

    applyGainStepping();
    settings->addChangeListener (this);
    updateBandStates();
    updateEqChannel();
    startTimerHz (30);
}

CoherenceEditor::~CoherenceEditor()
{
    settings->removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void CoherenceEditor::setScale (float s)
{
    s = juce::jlimit (minScale, maxScale, s);
    setSize (juce::roundToInt ((float) baseWidth * s), juce::roundToInt ((float) baseHeight * s));
    resized();   // setSize does not call resized() when the size is unchanged
}

void CoherenceEditor::resized()
{
    // Whatever set the size (setScale, the host, the corner), the panel follows the width.
    scale = juce::jlimit (minScale, maxScale, (float) getWidth() / (float) baseWidth);
    content->setTransform (juce::AffineTransform::scale (scale * panelToBase));
    if (auto* corner = getChildComponent (getNumChildComponents() - 1); corner != nullptr && corner != content.get())
        corner->toFront (false);
    if (proc.getUiScale() > 0.0f || std::abs (scale - 1.0f) > 1e-4f)
        proc.setUiScale (scale);   // remembered with the set (a fresh instance at 100 % stays unset)
}

void CoherenceEditor::requestScale (float s)
{
    setScale (s);
    proc.setUiScale (scale);
    settings->setDefaultScale (scale);
}

void CoherenceEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyGainStepping();
    applyTheme();
}

void CoherenceEditor::applyTheme()
{
    theme::setCurrent (settings->themeId());
    content->repaintEverything();
}

void CoherenceEditor::applyGainStepping()
{
    // Every dB control: the selected step under the mouse (drag and wheel), 0.01 dB with Shift. Typed values
    // stay exact. Values from the host or a controller play on the step grid (SteppedGainParameter), so the
    // readouts are redrawn for the new step.
    const float step = gainStepDb (settings->gainStepping());
    auto& c = *content;
    for (auto* set : { &c.bands, &c.sideBands })
        for (auto& b : *set)
            b.gain->setValueSteps (step, 0.01f);
    for (auto* k : { c.input.get(), c.mid.get(), c.side.get(), c.eqOutput.get(), c.threshold.get(), c.makeup.get() })
        k->setValueSteps (step, 0.01f);
    c.fader->setValueSteps (step, 0.01f);
    c.repaint();   // the readouts follow the new step
}

gui::Knob* CoherenceEditor::findKnob (const juce::String& pid) const
{
    for (auto* child : content->getChildren())
        if (auto* k = dynamic_cast<gui::Knob*> (child); k != nullptr && k->parameter().getParameterID() == pid)
            return k;
    return nullptr;
}

gui::ConsoleButton* CoherenceEditor::findAbButton (int slot) const
{
    return slot == 0 ? content->aButton.get() : content->bButton.get();
}

gui::Fader* CoherenceEditor::findFader() const
{
    return content->fader.get();
}

gui::Segmented* CoherenceEditor::findEqChannelSelector() const
{
    return content->eqChannel.get();
}

void CoherenceEditor::setEqMode (int mode)
{
    auto* p = proc.state().getParameter (id::eqMode);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) mode));
    p->endChangeGesture();
}

void CoherenceEditor::showSetupMenu()
{
    enum { sizeBase = 100, stepBase = 200, modeBase = 300, themeBase = 400 };
    juce::PopupMenu sizes;
    bool onPreset = false;
    for (int i = 0; i < (int) std::size (sizePresets); ++i)
    {
        const bool ticked = std::abs (scale - sizePresets[i].scale) < 0.005f;
        onPreset = onPreset || ticked;
        sizes.addItem (sizeBase + i, juce::String (sizePresets[i].name) + "  (" + juce::String (juce::roundToInt (sizePresets[i].scale * 100.0f)) + "%)", true, ticked);
    }
    if (! onPreset)
        sizes.addItem (sizeBase + 99, "Custom  (" + juce::String (juce::roundToInt (scale * 100.0f)) + "%)", false, true);

    juce::PopupMenu steps;
    for (auto g : { GainStepping::fine, GainStepping::standard, GainStepping::precision })
        steps.addItem (stepBase + (int) g, gainSteppingLabel (g), true, settings->gainStepping() == g);
    steps.addSeparator();
    steps.addItem (-1, "Shift: 0.01 dB. Typed values are always exact.", false, false);

    // EQ mode (saved with the set). Weiss mode adds 15 samples of latency, which the host compensates.
    auto* eqMode = proc.state().getParameter (id::eqMode);
    const int currentMode = juce::roundToInt (eqMode->convertFrom0to1 (eqMode->getValue()));
    juce::PopupMenu modes;
    modes.addItem (modeBase + 0, "COHERENCE  (zero latency)", true, currentMode == 0);
    modes.addItem (modeBase + 1, "WEISS  (Weiss EQ MP model, 2x, 15 samples latency)", true, currentMode == 1);

    juce::PopupMenu themes;
    for (int i = 0; i < theme::numThemes(); ++i)
        themes.addItem (themeBase + i, theme::themeAt (i).name, true, settings->themeId() == theme::themeAt (i).id);

    juce::PopupMenu m;
    m.addSectionHeader ("EQ");
    m.addSubMenu ("EQ mode", modes);
    m.addSectionHeader ("APPEARANCE");
    m.addSubMenu ("Theme", themes);
    m.addSubMenu ("Window size", sizes);
    m.addSectionHeader ("CONTROLS");
    m.addSubMenu ("Gain stepping", steps);
    m.addSeparator();
    m.addItem (-2, juce::String::fromUTF8 ("COHERENCE v" JucePlugin_VersionString "  \xc2\xb7  Phase Systems"), false, false);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (content->sizeButton.get()),
                     [safe = juce::Component::SafePointer<CoherenceEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         if (r >= sizeBase && r < sizeBase + (int) std::size (sizePresets))
                             safe->requestScale (sizePresets[r - sizeBase].scale);
                         else if (r >= stepBase && r <= stepBase + 2)
                             safe->settings->setGainStepping ((GainStepping) (r - stepBase));
                         else if (r == modeBase || r == modeBase + 1)
                             safe->setEqMode (r - modeBase);
                         else if (r >= themeBase && r < themeBase + theme::numThemes())
                         {
                             safe->settings->setThemeId (theme::themeAt (r - themeBase).id);   // every open instance follows
                             safe->applyTheme();
                         }
                     });
}

void CoherenceEditor::hoverChanged (juce::RangedAudioParameter* param, const juce::String& fallback)
{
    hoverParam = param;
    hoverFallback = fallback;
    if (param == nullptr && fallback.isEmpty())
        hoverValueSource = nullptr;
    updateStatus();
}

void CoherenceEditor::hoverValue (std::function<juce::String()> source)
{
    hoverValueSource = std::move (source);
}

void CoherenceEditor::updateStatus()
{
    juce::String s;
    const juce::String name = hoverParam != nullptr ? hoverParam->getName (64) : hoverFallback;
    if (hoverValueSource != nullptr)
        s = name + "  " + hoverValueSource();
    else if (hoverParam != nullptr)
        s = name + "  " + hoverParam->getCurrentValueAsText();
    else
        s = hoverFallback;
    content->status->setText (s);
}

void CoherenceEditor::updateBandStates()
{
    auto& s = proc.state();
    for (const bool sideSet : { false, true })
        for (int i : { 0, 3 })
        {
            auto& band = (sideSet ? content->sideBands : content->bands)[(size_t) i];
            const auto key = sideSet ? id::sideBand (id::bands[i]) : juce::String (id::bands[i]);
            const int type = juce::roundToInt (s.getRawParameterValue (id::bandType (key))->load());
            band.gain->setEnabledLook (type != 2);
            band.q->setLabel (juce::String (i == 3 ? "HF " : "LF ") + (type == 2 ? "RES" : "Q"));
            auto* qParam = &band.q->parameter();
            if (type == 0)
                band.q->setValueOverride ({});
            else
            {
                const auto shape = type == 1 ? dsp::Shape::lowShelf : dsp::Shape::lowCut;
                band.q->setValueOverride ([qParam, shape]
                {
                    const float q = (float) dsp::effectiveQ (shape, qParam->convertFrom0to1 (qParam->getValue()));
                    return qParam->getText (qParam->convertTo0to1 (q), 16);
                });
            }
        }
}

void CoherenceEditor::updateEqChannel()
{
    auto& s = proc.state();
    const bool ms = s.getRawParameterValue (id::eqMs)->load() >= 0.5f;
    const bool sideShown = ms && s.getRawParameterValue (id::eqChannel)->load() >= 0.5f;
    for (size_t i = 0; i < 4; ++i)
    {
        for (auto* set : { &content->bands, &content->sideBands })
        {
            auto& band = (*set)[i];
            const bool show = (set == &content->sideBands) == sideShown;
            band.gain->setVisible (show);
            band.freq->setVisible (show);
            band.q->setVisible (show);
            if (band.type != nullptr)
                band.type->setVisible (show);
        }
    }
    // In stereo there is one channel, so the selector rests.
    content->eqChannel->setEnabled (ms);
    content->header->repaint();
}

void CoherenceEditor::timerCallback()
{
    auto& m = proc.meters();
    const float dt = 1.0f / 30.0f;
    const int nCh = m.numChannels.load();

    float peaks[2], rms[2];
    for (int c = 0; c < 2; ++c)
    {
        peaks[c] = m.inPeak[(size_t) c].exchange (0.0f);
        rms[c] = m.inRms[(size_t) c].load();
    }
    content->inMeter->update (peaks, rms, nCh, dt);
    for (int c = 0; c < 2; ++c)
    {
        peaks[c] = m.outPeak[(size_t) c].exchange (0.0f);
        rms[c] = m.outRms[(size_t) c].load();
    }
    content->outMeter->update (peaks, rms, nCh, dt);
    content->msMeter->update (m.midRms.load(), m.sideRms.load(), nCh == 2, dt);

    auto& s = proc.state();
    const bool dynOn = s.getRawParameterValue (id::dynOn)->load() >= 0.5f;
    content->grMeter->setActive (dynOn);
    content->grMeter->update (m.gainReductionDb.exchange (0.0f), dt);

    content->release->setEnabledLook (s.getRawParameterValue (id::autoRelease)->load() < 0.5f);
    content->eqOutput->setEnabledLook (s.getRawParameterValue (id::eqOn)->load() >= 0.5f);   // it follows EQ IN
    content->routing->repaint();
    content->aButton->refresh();
    content->bButton->refresh();
    const bool bypassNow = s.getRawParameterValue (id::bypass)->load() >= 0.5f;
    const int eqModeNow = juce::roundToInt (s.getRawParameterValue (id::eqMode)->load());
    if (bypassNow != lastBypass || eqModeNow != lastEqMode)
    {
        lastBypass = bypassNow;
        lastEqMode = eqModeNow;
        content->header->repaint();
    }
    const int latencyNow = proc.getLatencySamples();   // changes with the EQ mode (reported from the audio thread)
    const double rateNow = proc.currentSampleRate();
    if (latencyNow != lastLatency || rateNow != lastRate)
    {
        lastLatency = latencyNow;
        lastRate = rateNow;
        content->panel->repaint();
    }
    const int eqChannelNow = (s.getRawParameterValue (id::eqMs)->load() >= 0.5f ? 2 : 0) + (s.getRawParameterValue (id::eqChannel)->load() >= 0.5f ? 1 : 0);
    if (eqChannelNow != lastEqChannel)
    {
        lastEqChannel = eqChannelNow;
        updateEqChannel();
    }
    const int types = juce::roundToInt (s.getRawParameterValue (id::bandType ("lf"))->load()) * 27
                      + juce::roundToInt (s.getRawParameterValue (id::bandType ("hf"))->load()) * 9
                      + juce::roundToInt (s.getRawParameterValue (id::bandType ("side_lf"))->load()) * 3
                      + juce::roundToInt (s.getRawParameterValue (id::bandType ("side_hf"))->load());
    if (types != lastTypes)
    {
        lastTypes = types;
        updateBandStates();
    }
    updateStatus();   // a value under the mouse can move: from a drag, automation or the UC1
}

void CoherenceEditor::tickMetersForPreview (int frames)
{
    for (int i = 0; i < frames; ++i)
        timerCallback();
}
} // namespace coherence
