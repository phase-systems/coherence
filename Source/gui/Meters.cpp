#include "Meters.h"

namespace coherence::gui
{
using namespace theme;

namespace
{
constexpr float floorDb = -60.0f;
float toDb (float lin) { return lin > 1.0e-6f ? 20.0f * std::log10 (lin) : -120.0f; }

juce::String dbString (float db, int decimals = 1)
{
    if (db <= -119.0f)
        return juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e");
    auto s = juce::String (db, decimals);
    if (db > 0.0f) s = "+" + s;
    return s + " dB";
}
} // namespace

void Ballistics::push (float peakDb, float dt)
{
    displayDb = peakDb >= displayDb ? peakDb : juce::jmax (peakDb, displayDb - 24.0f * dt);
    if (peakDb >= holdDb)
    {
        holdDb = peakDb;
        holdTime = 1.6f;
    }
    else if ((holdTime -= dt) < 0.0f)
    {
        holdDb = juce::jmax (displayDb, holdDb - 30.0f * dt);
    }
}

//==============================================================================
LevelMeter::LevelMeter (const juce::String& t, ScaleSide s, HoverSink* h) : title (t), side (s), hover (h) {}

void LevelMeter::update (const float* peakLin, const float* rmsLin, int numChannels, float dt)
{
    channels = juce::jlimit (1, 2, numChannels);
    for (int c = 0; c < 2; ++c)
    {
        const int src = juce::jmin (c, channels - 1);
        const float p = toDb (peakLin[src]);
        ch[(size_t) c].push (p, dt);
        rmsDb[(size_t) c] = toDb (rmsLin[src]);
        maxPeakDb = juce::jmax (maxPeakDb, p);
    }
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const float H = bottom - top, wide = 16.0f, gap = 2.2f, narrow = 6.0f, space = 8.0f;
    const auto frac = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (yOfDb (juce::jmax (db, floorDb - 0.4f)) - top) / H); };
    const bool input = side == ScaleSide::left;
    const float xNarrow = input ? wide * 2.0f + gap + space : 0.0f;
    const float x1 = input ? 0.0f : narrow + space, x2 = x1 + wide + gap;
    drawMeterBar (g, { x1, 0.0f, wide, H }, frac (rmsDb[0]), frac (ch[0].displayDb));
    drawMeterBar (g, { x2, 0.0f, wide, H }, frac (rmsDb[1]), frac (ch[1].displayDb));

    // The narrow bar: the peak hold, slowly falling, with a bright cap.
    const auto& t = current();
    const float hold = juce::jmax (ch[0].holdDb, ch[1].holdDb);
    const float y = frac (hold) * H;
    g.setColour (t.meterUnlit.darker (0.1f));
    g.fillRect (juce::Rectangle<float> (xNarrow, 0.0f, narrow, H));
    if (hold > floorDb)
    {
        g.setColour (t.meterLitBottom.darker (0.2f));
        g.fillRect (juce::Rectangle<float> (xNarrow, y, narrow, H - y));
        g.setColour (hold > 0.0f ? t.scaleHot : t.meterCap);
        g.fillRect (juce::Rectangle<float> (xNarrow, y - 3.0f, narrow, 6.0f));
    }
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    maxPeakDb = -120.0f;
    for (auto& c : ch)
        c.holdDb = c.displayDb;
    repaint();
}

void LevelMeter::mouseEnter (const juce::MouseEvent&)
{
    if (hover == nullptr)
        return;
    hover->hoverValue ([this] { return dbString (maxPeakDb); });
    hover->hoverChanged (nullptr, title + " PEAK");
}

void LevelMeter::mouseExit (const juce::MouseEvent&)
{
    if (hover != nullptr)
        hover->hoverChanged (nullptr, {});
}

//==============================================================================
GainReductionMeter::GainReductionMeter (HoverSink* h) : hover (h) {}

void GainReductionMeter::update (float grDb, float dt)
{
    grDb = juce::jmin (0.0f, grDb);
    const float before = display;
    display = grDb <= display ? grDb : juce::jmin (0.0f, display + 30.0f * dt);
    if (display > grDb) display = juce::jmax (grDb, display);
    if (std::abs (display - before) > 1.0e-3f)
        repaint();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    const auto& t = current();
    static constexpr float steps[] = { 20.0f, 14.0f, 10.0f, 6.0f, 3.0f };
    const float x = 30.0f, y0 = 22.0f, dy = 17.0f;   // room around the LEDs for their glow
    for (int i = 0; i < 5; ++i)
    {
        const juce::Colour c = i < 2 ? juce::Colour (0xffe03a4a) : juce::Colour (0xfff0b324);
        const float y = y0 + (float) i * dy;
        drawLed (g, x - 10.0f + 0.0f, y, 4.6f, c, active && display <= -steps[i]);
        theme::text (g, juce::String ((int) steps[i]), x + 9.0f, y, sans (9.8f), t.number);
    }
}

void GainReductionMeter::mouseEnter (const juce::MouseEvent&)
{
    if (hover == nullptr)
        return;
    hover->hoverValue ([this] { return active ? dbString (display) : juce::String ("OFF"); });
    hover->hoverChanged (nullptr, "GAIN REDUCTION");
}

void GainReductionMeter::mouseExit (const juce::MouseEvent&)
{
    if (hover != nullptr)
        hover->hoverChanged (nullptr, {});
}

//==============================================================================
MidSideMeter::MidSideMeter (HoverSink* h) : hover (h) {}

void MidSideMeter::update (float midLin, float sideLin, bool stereo, float)
{
    midDb = toDb (midLin);
    sideDb = toDb (sideLin);
    isStereo = stereo;
    repaint();
}

void MidSideMeter::paint (juce::Graphics& g)
{
    const auto o = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff232324));
    g.fillRoundedRectangle (o, 1.2f);
    g.setColour (juce::Colour (0xff404041));
    g.drawRoundedRectangle (o.reduced (0.3f), 1.2f, 0.6f);
    const auto in = o.reduced (5.0f);
    g.setColour (juce::Colour (0xff0f1011));
    g.fillRect (in);
    const juce::Rectangle<float> m (in.getX() + 8.0f, in.getY() + 14.0f, 11.0f, in.getHeight() - 22.0f), s = m.translated (14.0f, 0.0f);
    const auto frac = [] (float db) { return juce::jlimit (0.0f, 1.0f, -db / -floorDb); };
    drawMeterBar (g, m, isStereo ? frac (midDb) : 1.0f, isStereo ? frac (midDb) : 1.0f);
    drawMeterBar (g, s, isStereo ? frac (sideDb) : 1.0f, isStereo ? frac (sideDb) : 1.0f);
    const juce::Colour label (0xff8a8b8e);
    theme::text (g, "M", m.getCentreX(), in.getY() + 7.0f, sans (8.4f, true), label);
    theme::text (g, "S", s.getCentreX(), in.getY() + 7.0f, sans (8.4f, true), label);
}

void MidSideMeter::mouseEnter (const juce::MouseEvent&)
{
    if (hover == nullptr)
        return;
    hover->hoverValue ([this] { return isStereo ? "M " + dbString (midDb) + "  S " + dbString (sideDb) : juce::String ("MONO"); });
    hover->hoverChanged (nullptr, "MID SIDE");
}

void MidSideMeter::mouseExit (const juce::MouseEvent&)
{
    if (hover != nullptr)
        hover->hoverChanged (nullptr, {});
}
} // namespace coherence::gui
