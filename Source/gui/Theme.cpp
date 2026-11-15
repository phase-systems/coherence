#include "Theme.h"

namespace coherence::theme
{
using juce::Colour;
using juce::ColourGradient;
using juce::Path;
using RF = juce::Rectangle<float>;

namespace
{
//==============================================================================
// THE THEMES. Classic is the approved design (mac-results/gui-preview/COHERENCE-preview-A-default.png): don't change it.
// The others start from it, so that band colours, meters, displays and anything else they don't mention stay as they are.
Palette classic()
{
    Palette p;
    p.id = "classic";
    p.name = "Classic";
    p.panelTop = Colour (0xff2f3239); p.panel = Colour (0xff292a2f); p.header = Colour (0xff25262c); p.meterColumn = Colour (0xff18191b);
    p.block = Colour (0xff2e3037); p.stripTop = Colour (0xffb4b5b6); p.stripBottom = Colour (0xff939496);
    p.plateTop = Colour (0xffa9abad); p.plateBottom = Colour (0xff8f9092); p.barTop = Colour (0xff131417); p.barBottom = Colour (0xff0e0f11);
    p.ink = Colour (0xffa1a2a6); p.inkDim = Colour (0xff94959a); p.stripInk = Colour (0xff262729); p.rule = Colour (0xff808186);
    p.dot = Colour (0xffa6a7ab); p.number = Colour (0xffa8a9ad); p.plateInk = Colour (0xff232325); p.barDim = Colour (0xff5a5b5f); p.abActive = Colour (0xff35d873);
    p.capHf = Colour (0xff7c1b1a); p.capHmf = Colour (0xff1c422a); p.capLmf = Colour (0xff2c6793); p.capLf = Colour (0xff4f3531);
    p.capGrey = Colour (0xffbcbcba); p.capDark = Colour (0xff36373a); p.capLevel = Colour (0xff2c6793);
    p.meterUnlit = Colour (0xff331711); p.meterLitTop = Colour (0xffe06f18); p.meterLitBottom = Colour (0xffb1400d); p.meterPeak = Colour (0xffe9a671); p.meterCap = Colour (0xfff5a24f);
    p.scaleHot = Colour (0xffe0264f); p.scaleCool = Colour (0xff2aa35c); p.lcdLit = Colour (0xff39bd63); p.lcdDim = Colour (0xff2a9149); p.lcdAlert = Colour (0xfff0a830);
    p.grain = 0.16f;
    return p;
}

Palette warmGraphite()
{
    auto p = classic();
    p.id = "warm-graphite";
    p.name = "Warm Graphite";
    p.panelTop = Colour (0xff35322e); p.panel = Colour (0xff2e2b28); p.header = Colour (0xff2b2825); p.meterColumn = Colour (0xff1b1917);
    p.block = Colour (0xff35322d); p.stripTop = Colour (0xffb7b3ab); p.stripBottom = Colour (0xff96928a);
    p.plateTop = Colour (0xffada99f); p.plateBottom = Colour (0xff928e84); p.barTop = Colour (0xff161412); p.barBottom = Colour (0xff100f0d);
    p.ink = Colour (0xffa8a59d); p.inkDim = Colour (0xff9b9890); p.stripInk = Colour (0xff292622); p.rule = Colour (0xff88847c);
    p.dot = Colour (0xffaca9a1); p.number = Colour (0xffaeaba3);
    return p;
}

Palette champagne()
{
    auto p = classic();
    p.id = "champagne";
    p.name = "Champagne";
    p.panelTop = Colour (0xff2c2c2e); p.panel = Colour (0xff262629); p.header = Colour (0xff232326); p.meterColumn = Colour (0xff171718);
    p.block = Colour (0xff2b2b2d); p.stripTop = Colour (0xffcbc0ab); p.stripBottom = Colour (0xffa89d88);
    p.plateTop = Colour (0xffc6bba6); p.plateBottom = Colour (0xffa59a85); p.barTop = Colour (0xff121214); p.barBottom = Colour (0xff0e0e10);
    p.ink = Colour (0xffa2a1a0); p.inkDim = Colour (0xff959493); p.stripInk = Colour (0xff2c261b); p.rule = Colour (0xff807f7f);
    p.dot = Colour (0xffa7a6a5); p.number = Colour (0xffa9a8a7);
    return p;
}

const std::vector<Palette>& table()
{
    static const std::vector<Palette> themes { classic(), warmGraphite(), champagne() };   // Classic stays first: it's the default
    return themes;
}

int currentIndex = 0;

//==============================================================================
const char* glyph (char c)
{
    switch (c)
    {
        case 'A': return "01110100011000111111100011000110001"; case 'B': return "11110100011000111110100011000111110";
        case 'C': return "01110100011000010000100001000101110"; case 'D': return "11110100011000110001100011000111110";
        case 'E': return "11111100001000011110100001000011111"; case 'F': return "11111100001000011110100001000010000";
        case 'G': return "01110100011000010111100011000101111"; case 'H': return "10001100011000111111100011000110001";
        case 'I': return "01110001000010000100001000010001110"; case 'J': return "00111000100001000010000101001001100";
        case 'K': return "10001100101010011000101001001010001"; case 'L': return "10000100001000010000100001000011111";
        case 'M': return "10001110111010110101100011000110001"; case 'N': return "10001110011010110011100011000110001";
        case 'O': return "01110100011000110001100011000101110"; case 'P': return "11110100011000111110100001000010000";
        case 'Q': return "01110100011000110001101011001001101"; case 'R': return "11110100011000111110101001001010001";
        case 'S': return "01111100001000001110000010000111110"; case 'T': return "11111001000010000100001000010000100";
        case 'U': return "10001100011000110001100011000101110"; case 'V': return "10001100011000110001100010101000100";
        case 'W': return "10001100011000110101101011101110001"; case 'X': return "10001100010101000100010101000110001";
        case 'Y': return "10001100010101000100001000010000100"; case 'Z': return "11111000010001000100010001000011111";
        case '0': return "01110100011001110101110011000101110"; case '1': return "00100011000010000100001000010001110";
        case '2': return "01110100010000100010001000100011111"; case '3': return "11111000100010000010000011000101110";
        case '4': return "00010001100101010010111110001000010"; case '5': return "11111100001111000001000011000101110";
        case '6': return "00110010001000011110100011000101110"; case '7': return "11111000010001000100010000100001000";
        case '8': return "01110100011000101110100011000101110"; case '9': return "01110100011000101111000010001001100";
        case '.': return "00000000000000000000000000110001100"; case '%': return "11000110010001000100010001001100011";
        case '+': return "00000001000010011111001000010000000"; case '-': return "00000000000000011111000000000000000";
        case '/': return "00000000010001000100010001000000000"; case ':': return "00000011000110000000011000110000000";
        case '>': return "01000001000001000001000100010001000"; case '~': return "00000010101010110101010100000000000";   // ~ is infinity
        default: return nullptr;
    }
}

Path ringSector (float cx, float cy, float r0, float r1, float a0, float a1)
{
    Path p;
    p.addCentredArc (cx, cy, r1, r1, 0.0f, a0, a1, true);
    p.addCentredArc (cx, cy, r0, r0, 0.0f, a1, a0, false);
    p.closeSubPath();
    return p;
}

Colour capColour (const Palette& p, Cap c)
{
    switch (c)
    {
        case Cap::hf:    return p.capHf;
        case Cap::hmf:   return p.capHmf;
        case Cap::lmf:   return p.capLmf;
        case Cap::lf:    return p.capLf;
        case Cap::dark:  return p.capDark;
        case Cap::level: return p.capLevel;
        case Cap::grey:  break;
    }
    return p.capGrey;
}
} // namespace

//==============================================================================
int numThemes() { return (int) table().size(); }
const Palette& themeAt (int index) { return table()[(size_t) juce::jlimit (0, numThemes() - 1, index)]; }
const Palette& current() { return themeAt (currentIndex); }

void setCurrent (const juce::String& id)
{
    currentIndex = 0;
    for (int i = 0; i < numThemes(); ++i)
        if (id == themeAt (i).id)
            currentIndex = i;
}

juce::Font sans (float height, bool bold, float kerning)
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", height, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kerning);
}

void text (juce::Graphics& g, const juce::String& s, float x, float cy, const juce::Font& f, Colour c, juce::Justification j)
{
    g.setFont (f);
    g.setColour (c);
    const float w = 200.0f, h = f.getHeight() + 4.0f;
    RF r (x - w * 0.5f, cy - h * 0.5f, w, h);
    if (j == juce::Justification::centredLeft) r.setX (x);
    if (j == juce::Justification::centredRight) r.setX (x - w);
    g.drawText (s, r, j, false);
}

//==============================================================================
juce::String lcdString (const juce::String& s)
{
    return s.toUpperCase()
        .replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-")   // the minus sign in the readouts
        .replace (juce::String::fromUTF8 ("\xe2\x88\x9e"), "~")   // infinity
        .replace (juce::String::fromUTF8 ("\xc2\xb5"), "U")       // micro
        .replace (juce::String::fromUTF8 ("\xce\xbc"), "U");
}

float lcdWidth (const juce::String& s, float p) { return (float) s.length() * 6.0f * p - p; }

void lcdText (juce::Graphics& g, const juce::String& s, float x, float cy, float p, Colour lit)
{
    const float top = cy - 3.5f * p;
    g.setColour (lit);
    for (int i = 0; i < s.length(); ++i)
        if (const char* m = glyph ((char) s[i]))
            for (int r = 0; r < 7; ++r)
                for (int c = 0; c < 5; ++c)
                    if (m[r * 5 + c] == '1')
                        g.fillRoundedRectangle (x + (float) i * 6.0f * p + (float) c * p, top + (float) r * p, p * 0.9f, p * 0.9f, p * 0.2f);
}

void lcdPanel (juce::Graphics& g, RF r)
{
    g.setColour (Colour (0xff0c0d0f));
    g.fillRoundedRectangle (r.expanded (1.4f), 2.6f);
    g.setColour (Colour (0xff3b3d43));
    g.drawRoundedRectangle (r.expanded (1.4f).translated (0.0f, 0.5f), 2.6f, 0.5f);
    ColourGradient bg (Colour (0xff17371f), r.getCentreX(), r.getCentreY(), Colour (0xff0a150d), r.getX(), r.getY(), true);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 1.8f);
}

//==============================================================================
void drawKnob (juce::Graphics& g, float cx, float cy, const KnobLook& look, float norm, const juce::String& caption)
{
    static constexpr float travel = juce::MathConstants<float>::pi * 150.0f / 180.0f;
    static constexpr float capR = 14.2f, skirtR = 17.8f, dotR = 21.8f;
    const auto& t = current();
    const Colour numCol = look.onStrip ? t.stripInk.withAlpha (0.9f) : t.number;
    const Colour dotCol = look.onStrip ? numCol.withAlpha (0.8f) : t.dot;
    const bool greyCap = look.cap == Cap::grey;
    const Colour cap = capColour (t, look.cap);

    for (const auto& m : look.marks)
    {
        const float a = (m.pos * 2.0f - 1.0f) * travel, sx = std::sin (a), cyy = -std::cos (a);
        g.setColour (dotCol);
        g.fillEllipse (cx + sx * dotR - 1.3f, cy + cyy * dotR - 1.3f, 2.6f, 2.6f);
        if (m.label.isNotEmpty())
        {
            const auto f = sans (9.2f);
            const float tw = juce::GlyphArrangement::getStringWidth (f, m.label), th = 7.0f;
            const float off = dotR + 2.7f + std::abs (sx) * tw * 0.5f + std::abs (cyy) * th * 0.5f;
            float lx = cx + sx * off, ly = cy + cyy * off;
            if (cyy > 0.5f)
            {
                ly = juce::jmin (ly, cy + 24.2f);
                lx += (sx < 0.0f ? -1.0f : 1.0f) * (1.5f + tw * 0.16f);
            }
            text (g, m.label, lx, ly, f, numCol);
        }
    }

    // Seen slightly from below: the skirt sits lower than the cap, thin at the top and a chunky grip at the bottom.
    const float sy = cy + 1.9f, capY = cy - 0.1f;
    {
        ColourGradient s (juce::Colours::black.withAlpha (0.6f), cx, sy + 2.0f, juce::Colours::transparentBlack, cx, sy + 2.0f + skirtR + 6.0f, true);
        g.setGradientFill (s);
        g.fillEllipse (cx - skirtR - 6.0f, sy + 2.0f - skirtR - 6.0f, (skirtR + 6.0f) * 2.0f, (skirtR + 6.0f) * 2.0f);
    }
    const float angle = (norm * 2.0f - 1.0f) * travel;
    const float pi = juce::MathConstants<float>::pi;
    {
        ColourGradient s (Colour (0xff4f5254), cx, sy - skirtR, Colour (0xff333535), cx, sy + skirtR, false);
        g.setGradientFill (s);
        g.fillEllipse (cx - skirtR, sy - skirtR, skirtR * 2.0f, skirtR * 2.0f);
    }
    const float n1 = pi * 0.70f, n2 = pi * 1.30f;
    {
        auto sec = ringSector (cx, sy, capR - 1.0f, skirtR - 0.3f, n1, n2);
        ColourGradient s (Colour (0xff6e7070), cx, sy + capR - 3.0f, Colour (0xff434545), cx, sy + skirtR, false);
        g.setGradientFill (s);
        g.fillPath (sec);
    }
    for (float a2 : { n1, pi, n2 })
    {
        Path l;
        l.startNewSubPath (cx + std::sin (a2) * (capR - 1.0f), sy - std::cos (a2) * (capR - 1.0f));
        l.lineTo (cx + std::sin (a2) * skirtR, sy - std::cos (a2) * skirtR);
        g.setColour (Colour (0xff202121));
        g.strokePath (l, juce::PathStrokeType (1.0f));
    }
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (cx - skirtR, sy - skirtR, skirtR * 2.0f, skirtR * 2.0f, 0.5f);
    {
        ColourGradient r (Colour (0xffe0e0db), cx, capY - capR - 0.8f, Colour (0xff5e6062), cx, capY + capR + 0.8f, false);
        g.setGradientFill (r);
        g.fillEllipse (cx - capR - 0.8f, capY - capR - 0.8f, (capR + 0.8f) * 2.0f, (capR + 0.8f) * 2.0f);
    }
    {
        ColourGradient c (cap.brighter (greyCap ? 0.05f : 0.10f), cx, capY - capR, cap.darker (greyCap ? 0.05f : 0.12f), cx, capY + capR, false);
        g.setGradientFill (c);
        g.fillEllipse (cx - capR, capY - capR, capR * 2.0f, capR * 2.0f);
    }
    if (greyCap)
    {
        g.setColour (juce::Colours::black.withAlpha (0.10f));
        g.drawEllipse (cx - capR + 1.6f, capY - capR + 1.6f, (capR - 1.6f) * 2.0f, (capR - 1.6f) * 2.0f, 0.5f);
    }
    {
        Path p;
        p.addRoundedRectangle (cx - 1.6f, capY - capR + 0.1f, 3.2f, capR + 0.4f, 0.7f);
        p.applyTransform (juce::AffineTransform::rotation (angle, cx, capY));
        g.setColour (greyCap ? Colour (0xff4c4c4e) : Colour (0xffd2d2d0));
        g.fillPath (p);
    }
    if (caption.isNotEmpty())
        text (g, caption, cx, cy + look.captionDy, sans (9.8f), look.onStrip ? t.stripInk : t.inkDim);
}

//==============================================================================
void drawKeyText (juce::Graphics& g, RF r, const juce::String& s, float th, Colour c)
{
    const float lip = juce::jmin (3.6f, r.getHeight() * 0.17f);
    const auto face = r.withTrimmedBottom (lip);
    text (g, s, face.getCentreX(), face.getCentreY() + 0.2f, sans (th, true).withHorizontalScale (0.93f), c);
}

void drawKey (juce::Graphics& g, RF r, const juce::String& label, bool lit, float th)
{
    g.setColour (Colour (0xff131417).withAlpha (0.9f));
    g.fillRoundedRectangle (r.expanded (1.5f), 3.2f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.expanded (1.5f).translated (0.0f, 0.6f), 3.2f, 0.5f);
    if (lit)
    {
        g.setColour (Colour (0xff8a3f10));
        g.fillRoundedRectangle (r.expanded (0.9f), 2.8f);
        ColourGradient b (Colour (0xfff8cfa5), r.getX(), r.getY(), Colour (0xffebb27a), r.getX(), r.getBottom(), false);
        g.setGradientFill (b);
        g.fillRoundedRectangle (r, 2.2f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRoundedRectangle (r.withHeight (0.8f).reduced (1.0f, 0.0f), 0.4f);
        const auto face = r.withTrimmedBottom (r.getHeight() * 0.04f);
        text (g, label, face.getCentreX(), face.getCentreY(), sans (th, true).withHorizontalScale (0.93f), Colour (0xffb65a1e));
        return;
    }
    const float lip = juce::jmin (3.6f, r.getHeight() * 0.17f);
    g.setColour (Colour (0xff5a5a5b));
    g.fillRoundedRectangle (r, 2.2f);
    const auto face = r.withTrimmedBottom (lip);
    ColourGradient b (Colour (0xffb9b9b9), r.getX(), r.getY(), Colour (0xff8e8e8e), r.getX(), face.getBottom(), false);
    g.setGradientFill (b);
    g.fillRoundedRectangle (face, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.fillRoundedRectangle (face.withHeight (0.7f).reduced (1.0f, 0.0f), 0.3f);
    text (g, label, face.getCentreX(), face.getCentreY() + 0.2f, sans (th, true).withHorizontalScale (0.93f), Colour (0xff36373a));
}

void drawLed (juce::Graphics& g, float cx, float cy, float r, Colour c, bool lit)
{
    g.setColour (Colour (0xff101113));
    g.fillEllipse (cx - r - 1.0f, cy - r - 1.0f, (r + 1.0f) * 2.0f, (r + 1.0f) * 2.0f);
    if (lit)
    {
        ColourGradient h (c.withAlpha (0.45f), cx, cy, c.withAlpha (0.0f), cx, cy + r * 3.0f, true);
        g.setGradientFill (h);
        g.fillEllipse (cx - r * 3.0f, cy - r * 3.0f, r * 6.0f, r * 6.0f);
    }
    const Colour base = lit ? c : c.withMultipliedBrightness (0.36f).withMultipliedSaturation (0.75f);
    ColourGradient b (base.brighter (lit ? 0.75f : 0.32f), cx, cy - r * 0.45f, base.darker (0.25f), cx, cy + r, true);
    g.setGradientFill (b);
    g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.55f : 0.16f));
    g.fillEllipse (cx - r * 0.45f, cy - r * 0.68f, r * 0.8f, r * 0.5f);
}

void drawMeterBar (juce::Graphics& g, RF r, float rms, float peak)
{
    const auto& t = current();
    g.setColour (t.meterUnlit);
    g.fillRect (r);
    const float yR = r.getY() + r.getHeight() * rms, yP = r.getY() + r.getHeight() * peak;
    if (peak < rms)
    {
        g.setColour (t.meterPeak);
        g.fillRect (RF (r.getX(), yP, r.getWidth(), yR - yP));
    }
    ColourGradient c (t.meterLitTop, r.getX(), yR, t.meterLitBottom, r.getX(), r.getBottom(), false);
    g.setGradientFill (c);
    g.fillRect (RF (r.getX(), yR, r.getWidth(), r.getBottom() - yR));
    g.setColour (t.meterCap);
    g.fillRect (RF (r.getX(), yR, r.getWidth(), 1.1f));
    g.setColour (juce::Colours::black.withAlpha (0.13f));
    for (float y = r.getY(); y < r.getBottom(); y += 1.5f)
        g.fillRect (RF (r.getX(), y, r.getWidth(), 0.5f));
}

void drawRule (juce::Graphics& g, std::initializer_list<juce::Point<float>> pts)
{
    Path p;
    bool first = true;
    for (auto q : pts)
    {
        if (first) p.startNewSubPath (q);
        else p.lineTo (q);
        first = false;
    }
    g.setColour (current().rule);
    g.strokePath (p.createPathWithRoundedCorners (5.0f), juce::PathStrokeType (1.0f));
}

void drawGrain (juce::Graphics& g, RF r)
{
    static const juce::Image tile = []
    {
        juce::Image im (juce::Image::ARGB, 128, 128, true);
        juce::Random rnd (7);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
            {
                const auto v = (juce::uint8) (rnd.nextInt (2) * 255);
                im.setPixelAt (x, y, Colour::fromRGBA (v, v, v, (juce::uint8) rnd.nextInt (22)));
            }
        return im;
    }();
    g.setTiledImageFill (tile, 0, 0, current().grain);
    g.fillRect (r);
}

void drawMark (juce::Graphics& g, float x0, float y0, float w, float amp)
{
    Path a, b;
    for (int i = 0; i <= 40; ++i)
    {
        const float u = (float) i / 40.0f, ph = u * juce::MathConstants<float>::twoPi;
        const float y1 = y0 + std::sin (ph) * amp, y2 = y0 + std::sin (ph - juce::MathConstants<float>::halfPi) * amp;
        if (i == 0) { a.startNewSubPath (x0, y1); b.startNewSubPath (x0, y2); }
        else { a.lineTo (x0 + u * w, y1); b.lineTo (x0 + u * w, y2); }
    }
    g.setColour (current().plateInk);
    g.strokePath (a, juce::PathStrokeType (1.0f));
    g.setColour (Colour (0xff1f4f86));
    g.strokePath (b, juce::PathStrokeType (1.0f));
}
} // namespace coherence::theme
