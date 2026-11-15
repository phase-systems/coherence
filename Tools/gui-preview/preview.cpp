// A rendered preview of the proposed COHERENCE panel. Not part of the plug-in. All drawing is original vector code;
// the SSL 4K references were only measured. Coordinates are 720 x 666, half the reference images' pixels.
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
using juce::Colour; using juce::Graphics; using juce::String; using juce::Rectangle; using juce::Point; using juce::Path;
using juce::ColourGradient; using juce::Justification; using RF = Rectangle<float>;

namespace pal
{
struct P { Colour panelTop, panel, header, meterCol, block, stripTop, stripBot, ink, inkDim, stripInk, rule, dot, num; bool darkStrip;
           Colour plateTop { 0xffa9abad }, plateBot { 0xff8f9092 }, barTop { 0xff131417 }, barBot { 0xff0e0f11 }; };
const P e { Colour (0xff2f3239), Colour (0xff292a2f), Colour (0xff25262c), Colour (0xff18191b), Colour (0xff2e3037), Colour (0xffb4b5b6), Colour (0xff939496),
            Colour (0xffa1a2a6), Colour (0xff94959a), Colour (0xff262729), Colour (0xff808186), Colour (0xffa6a7ab), Colour (0xffa8a9ad), false };
// Warm Graphite: charcoal with tan and grey undertones. Band colours, meters and displays are as in Classic.
const P graphite { Colour (0xff35322e), Colour (0xff2e2b28), Colour (0xff2b2825), Colour (0xff1b1917), Colour (0xff35322d), Colour (0xffb7b3ab), Colour (0xff96928a),
                   Colour (0xffa8a59d), Colour (0xff9b9890), Colour (0xff292622), Colour (0xff88847c), Colour (0xffaca9a1), Colour (0xffaeaba3), false,
                   Colour (0xffada99f), Colour (0xff928e84), Colour (0xff161412), Colour (0xff100f0d) };
// Champagne: dark charcoal outside, a warm champagne silver-grey centre section.
const P champagne { Colour (0xff2c2c2e), Colour (0xff262629), Colour (0xff232326), Colour (0xff171718), Colour (0xff2b2b2d), Colour (0xffcbc0ab), Colour (0xffa89d88),
                    Colour (0xffa2a1a0), Colour (0xff959493), Colour (0xff2c261b), Colour (0xff807f7f), Colour (0xffa7a6a5), Colour (0xffa9a8a7), false,
                    Colour (0xffc6bba6), Colour (0xffa59a85), Colour (0xff121214), Colour (0xff0e0e10) };
const P b { Colour (0xff1e2022), Colour (0xff1a1c1f), Colour (0xff151519), Colour (0xff111111), Colour (0xff1a1b1e), Colour (0xff232427), Colour (0xff1c1d1f),
            Colour (0xffa1a2a6), Colour (0xff8f9094), Colour (0xffa1a2a6), Colour (0xff767779), Colour (0xff9a9b9f), Colour (0xffa4a5a9), true };
}
static const pal::P* P = &pal::e;
static bool msOn = false, sideSel = false, onB = false;

static juce::Font sans (float h, bool bold = false, float kern = 0.0f)
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", h, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kern);
}
static void text (Graphics& g, const String& s, float cx, float cy, const juce::Font& f, Colour c, Justification j = Justification::centred)
{
    g.setFont (f); g.setColour (c);
    const float W = 200.0f, H = f.getHeight() + 4.0f;
    RF r (cx - W * 0.5f, cy - H * 0.5f, W, H);
    if (j == Justification::centredLeft) r.setX (cx);
    if (j == Justification::centredRight) r.setX (cx - W);
    g.drawText (s, r, j, false);
}

// ---- Dot-matrix readout
static const char* glyph (char c)
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
        default: return nullptr;
    }
}
static float lcdWidth (const String& s, float p) { return (float) s.length() * 6.0f * p - p; }
static void lcdText (Graphics& g, const String& s, float x, float cy, float p, Colour lit)
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
static void lcdPanel (Graphics& g, RF r)
{
    g.setColour (Colour (0xff0c0d0f)); g.fillRoundedRectangle (r.expanded (1.4f), 2.6f);
    g.setColour (Colour (0xff3b3d43)); g.drawRoundedRectangle (r.expanded (1.4f).translated (0.0f, 0.5f), 2.6f, 0.5f);
    ColourGradient bg (Colour (0xff17371f), r.getCentreX(), r.getCentreY(), Colour (0xff0a150d), r.getX(), r.getY(), true);
    g.setGradientFill (bg); g.fillRoundedRectangle (r, 1.8f);
}

// ---- Knob
struct Mark { float pos; String label; };
static std::vector<Mark> linear (float lo, float hi, std::initializer_list<std::pair<float, const char*>> m)
{
    std::vector<Mark> v;
    for (auto& [x, t] : m) v.push_back ({ (x - lo) / (hi - lo), t });
    return v;
}
static std::vector<Mark> logm (float lo, float hi, std::initializer_list<std::pair<float, const char*>> m)
{
    std::vector<Mark> v;
    for (auto& [x, t] : m) v.push_back ({ std::log (x / lo) / std::log (hi / lo), t });
    return v;
}
static constexpr float travel = juce::MathConstants<float>::pi * 150.0f / 180.0f;
static float capR = 14.2f, skirtR = 17.8f, dotR = 21.8f;

static Path ringSector (float cx, float cy, float r0, float r1, float a0, float a1)
{
    Path p; p.addCentredArc (cx, cy, r1, r1, 0.0f, a0, a1, true); p.addCentredArc (cx, cy, r0, r0, 0.0f, a1, a0, false); p.closeSubPath(); return p;
}
static void knob (Graphics& g, float cx, float cy, Colour cap, float norm, const std::vector<Mark>& marks, const String& caption, bool greyCap = false, float captionDy = 34.0f, Colour numCol = {}, Colour capCol = {}, bool capsCaption = false)
{
    const Colour num = numCol.isTransparent() ? P->num : numCol;
    const Colour dotc = numCol.isTransparent() ? P->dot : numCol.withAlpha (0.8f);
    for (auto& m : marks)
    {
        const float a = (m.pos * 2.0f - 1.0f) * travel, sx = std::sin (a), cyy = -std::cos (a);
        g.setColour (dotc); g.fillEllipse (cx + sx * dotR - 1.3f, cy + cyy * dotR - 1.3f, 2.6f, 2.6f);
        if (m.label.isNotEmpty())
        {
            const auto f = sans (9.2f);
            const float tw = juce::GlyphArrangement::getStringWidth (f, m.label), th = 7.0f;
            const float off = dotR + 2.7f + std::abs (sx) * tw * 0.5f + std::abs (cyy) * th * 0.5f;
            float lx = cx + sx * off, ly = cy + cyy * off;
            if (cyy > 0.5f) { ly = juce::jmin (ly, cy + 24.2f); lx += (sx < 0.0f ? -1.0f : 1.0f) * (1.5f + tw * 0.16f); }
            text (g, m.label, lx, ly, f, num);
        }
    }
    // Seen slightly from below: the skirt sits lower than the cap, thin at the top and a chunky grip at the bottom.
    const float sy = cy + 1.9f, capY = cy - 0.1f;
    { ColourGradient s (juce::Colours::black.withAlpha (0.6f), cx, sy + 2.0f, juce::Colours::transparentBlack, cx, sy + 2.0f + skirtR + 6.0f, true);
      g.setGradientFill (s); g.fillEllipse (cx - skirtR - 6.0f, sy + 2.0f - skirtR - 6.0f, (skirtR + 6.0f) * 2.0f, (skirtR + 6.0f) * 2.0f); }
    const float angle = (norm * 2.0f - 1.0f) * travel;
    const float pi = juce::MathConstants<float>::pi;
    { ColourGradient s (Colour (0xff4f5254), cx, sy - skirtR, Colour (0xff333535), cx, sy + skirtR, false);
      g.setGradientFill (s); g.fillEllipse (cx - skirtR, sy - skirtR, skirtR * 2.0f, skirtR * 2.0f); }
    // the grip: fixed lit sections around the lower half, as the reference draws them
    const float n1 = pi * 0.70f, n2 = pi * 1.30f;
    { auto sec = ringSector (cx, sy, capR - 1.0f, skirtR - 0.3f, n1, n2);
      ColourGradient s (Colour (0xff6e7070), cx, sy + capR - 3.0f, Colour (0xff434545), cx, sy + skirtR, false);
      g.setGradientFill (s); g.fillPath (sec); }
    for (float a2 : { n1, pi, n2 })
    {
        Path l; l.startNewSubPath (cx + std::sin (a2) * (capR - 1.0f), sy - std::cos (a2) * (capR - 1.0f)); l.lineTo (cx + std::sin (a2) * skirtR, sy - std::cos (a2) * skirtR);
        g.setColour (Colour (0xff202121)); g.strokePath (l, juce::PathStrokeType (1.0f));
    }
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.drawEllipse (cx - skirtR, sy - skirtR, skirtR * 2.0f, skirtR * 2.0f, 0.5f);
    { ColourGradient r (Colour (0xffe0e0db), cx, capY - capR - 0.8f, Colour (0xff5e6062), cx, capY + capR + 0.8f, false);
      g.setGradientFill (r); g.fillEllipse (cx - capR - 0.8f, capY - capR - 0.8f, (capR + 0.8f) * 2.0f, (capR + 0.8f) * 2.0f); }
    { ColourGradient c (cap.brighter (greyCap ? 0.05f : 0.10f), cx, capY - capR, cap.darker (greyCap ? 0.05f : 0.12f), cx, capY + capR, false);
      g.setGradientFill (c); g.fillEllipse (cx - capR, capY - capR, capR * 2.0f, capR * 2.0f); }
    if (greyCap) { g.setColour (juce::Colours::black.withAlpha (0.10f)); g.drawEllipse (cx - capR + 1.6f, capY - capR + 1.6f, (capR - 1.6f) * 2.0f, (capR - 1.6f) * 2.0f, 0.5f); }
    { Path p; p.addRoundedRectangle (cx - 1.6f, capY - capR + 0.1f, 3.2f, capR + 0.4f, 0.7f); p.applyTransform (juce::AffineTransform::rotation (angle, cx, capY));
      g.setColour (greyCap ? Colour (0xff4c4c4e) : Colour (0xffd2d2d0)); g.fillPath (p); }
    if (caption.isNotEmpty())
        text (g, caption, cx, cy + captionDy, sans (capsCaption ? 9.8f : 9.8f), capCol.isTransparent() ? P->inkDim : capCol);
}

// ---- Keycap
static void key (Graphics& g, RF r, const String& label, bool lit, float th = 9.6f, Colour litInk = Colour (0xffb65a1e))
{
    // recess
    g.setColour (Colour (0xff131417).withAlpha (0.9f)); g.fillRoundedRectangle (r.expanded (1.5f), 3.2f);
    g.setColour (juce::Colours::white.withAlpha (0.05f)); g.drawRoundedRectangle (r.expanded (1.5f).translated (0.0f, 0.6f), 3.2f, 0.5f);
    if (lit)
    {
        g.setColour (Colour (0xff8a3f10)); g.fillRoundedRectangle (r.expanded (0.9f), 2.8f);
        ColourGradient b (Colour (0xfff8cfa5), r.getX(), r.getY(), Colour (0xffebb27a), r.getX(), r.getBottom(), false);
        g.setGradientFill (b); g.fillRoundedRectangle (r, 2.2f);
        g.setColour (juce::Colours::white.withAlpha (0.35f)); g.fillRoundedRectangle (r.withHeight (0.8f).reduced (1.0f, 0.0f), 0.4f);
        const auto face = r.withTrimmedBottom (r.getHeight() * 0.04f);
        text (g, label, face.getCentreX(), face.getCentreY(), sans (th, true).withHorizontalScale (0.93f), litInk);
        return;
    }
    const float lip = juce::jmin (3.6f, r.getHeight() * 0.17f);
    g.setColour (Colour (0xff5a5a5b)); g.fillRoundedRectangle (r, 2.2f);
    const auto face = r.withTrimmedBottom (lip);
    ColourGradient b (Colour (0xffb9b9b9), r.getX(), r.getY(), Colour (0xff8e8e8e), r.getX(), face.getBottom(), false);
    g.setGradientFill (b); g.fillRoundedRectangle (face, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.45f)); g.fillRoundedRectangle (face.withHeight (0.7f).reduced (1.0f, 0.0f), 0.3f);
    text (g, label, face.getCentreX(), face.getCentreY() + 0.2f, sans (th, true).withHorizontalScale (0.93f), Colour (0xff36373a));
}

// ---- LED
static void led (Graphics& g, float cx, float cy, float r, Colour c, bool lit)
{
    g.setColour (Colour (0xff101113)); g.fillEllipse (cx - r - 1.0f, cy - r - 1.0f, (r + 1.0f) * 2.0f, (r + 1.0f) * 2.0f);
    if (lit)
    {
        ColourGradient h (c.withAlpha (0.45f), cx, cy, c.withAlpha (0.0f), cx, cy + r * 3.0f, true);
        g.setGradientFill (h); g.fillEllipse (cx - r * 3.0f, cy - r * 3.0f, r * 6.0f, r * 6.0f);
    }
    const Colour base = lit ? c : c.withMultipliedBrightness (0.36f).withMultipliedSaturation (0.75f);
    ColourGradient b (base.brighter (lit ? 0.75f : 0.32f), cx, cy - r * 0.45f, base.darker (0.25f), cx, cy + r, true);
    g.setGradientFill (b); g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.55f : 0.16f)); g.fillEllipse (cx - r * 0.45f, cy - r * 0.68f, r * 0.8f, r * 0.5f);
}

// ---- Level meter
static void bar (Graphics& g, RF r, float rms, float peak)
{
    g.setColour (Colour (0xff331711)); g.fillRect (r);
    const float yR = r.getY() + r.getHeight() * rms, yP = r.getY() + r.getHeight() * peak;
    if (peak < rms) { g.setColour (Colour (0xffe9a671)); g.fillRect (RF (r.getX(), yP, r.getWidth(), yR - yP)); }
    ColourGradient c (Colour (0xffe06f18), r.getX(), yR, Colour (0xffb1400d), r.getX(), r.getBottom(), false);
    g.setGradientFill (c); g.fillRect (RF (r.getX(), yR, r.getWidth(), r.getBottom() - yR));
    g.setColour (Colour (0xfff5a24f)); g.fillRect (RF (r.getX(), yR, r.getWidth(), 1.1f));
    g.setColour (juce::Colours::black.withAlpha (0.13f));
    for (float y = r.getY(); y < r.getBottom(); y += 1.5f) g.fillRect (RF (r.getX(), y, r.getWidth(), 0.5f));
}
static void meter (Graphics& g, float colX, bool isInput, const String& title, float l, float r, float lp, float rp, float slow)
{
    const float top = 73.0f, bot = 618.0f, H = bot - top;
    text (g, title, colX + 45.0f + (isInput ? 17.0f : -17.0f), 60.5f, sans (10.6f), Colour (0xff8a8b8e));
    const float a = isInput ? colX + 30.0f : colX + 25.0f, wide = 16.0f, gap = 2.2f;
    const float x1 = isInput ? a : colX + 14.0f + 6.0f + 8.0f, x2 = x1 + wide + gap;
    const float xs = isInput ? x2 + wide + 8.0f : colX + 14.0f;
    bar (g, RF (x1, top, wide, H), l, lp);
    bar (g, RF (x2, top, wide, H), r, rp);
    { RF t (xs, top, 6.0f, H); g.setColour (Colour (0xff2e1612)); g.fillRect (t);
      const float y = top + H * slow; g.setColour (Colour (0xff8a3a12)); g.fillRect (RF (xs, y, 6.0f, bot - y));
      g.setColour (Colour (0xfff29a4b)); g.fillRect (RF (xs, y - 3.0f, 6.0f, 6.0f)); }
    const float nx = isInput ? a - 6.5f : x2 + wide + 6.5f;
    for (int d = 0; d <= 60; d += 6)
    {
        const float y = 76.5f + (float) d * 9.0f;
        text (g, String (d), nx, y, sans (9.3f, true), d <= 12 ? Colour (0xffe0264f) : Colour (0xff2aa35c), isInput ? Justification::centredRight : Justification::centredLeft);
    }
}

// ---- Fader
static float faderToPos (float db)
{
    static const float pos[] = { 0.0f, 0.12f, 0.24f, 0.38f, 0.52f, 0.62f, 0.74f, 0.87f, 1.0f }, dbs[] = { -60.0f, -40.0f, -30.0f, -20.0f, -10.0f, -5.0f, 0.0f, 6.0f, 12.0f };
    for (int i = 1; i < 9; ++i) if (db <= dbs[i]) return pos[i - 1] + (db - dbs[i - 1]) / (dbs[i] - dbs[i - 1]) * (pos[i] - pos[i - 1]);
    return 1.0f;
}
static void fader (Graphics& g, float cx, float top, float bot, float db, Colour numInk)
{
    const float h = bot - top;
    const auto yOf = [&] (float d) { return bot - 14.0f - faderToPos (d) * (h - 34.0f); };
    g.setColour (juce::Colours::black.withAlpha (0.18f)); g.fillRoundedRectangle (cx - 3.6f, top - 1.0f, 7.2f, h + 2.0f, 3.6f);
    g.setColour (Colour (0xff0b0b0c)); g.fillRoundedRectangle (cx - 2.5f, top, 5.0f, h, 2.5f);
    for (auto [d, t] : std::initializer_list<std::pair<float, const char*>> { { 12.f, "12" }, { 6.f, "6" }, { 0.f, "0" }, { -5.f, "5" }, { -10.f, "10" }, { -20.f, "20" }, { -30.f, "30" }, { -40.f, "40" }, { -60.f, "" } })
    {
        if (*t) text (g, t, cx + 26.0f, yOf (d), sans (10.0f), numInk);
        else { juce::Path p; const float x = cx + 26.0f, y = yOf (d); p.addEllipse (x - 5.5f, y - 2.2f, 5.2f, 4.4f); p.addEllipse (x + 0.3f, y - 2.2f, 5.2f, 4.4f); g.setColour (numInk); g.strokePath (p, juce::PathStrokeType (0.9f)); }
    }
    const float y = yOf (db), w = 28.0f, ch = 55.0f; RF c (cx - w * 0.5f, y - ch * 0.5f, w, ch);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 9, { 0, 5 }).drawForRectangle (g, c.toNearestInt());
    g.setColour (Colour (0xff57585a)); g.fillRoundedRectangle (c.expanded (0.4f), 2.4f);
    ColourGradient b (Colour (0xffb0b0ad), c.getX(), c.getY(), Colour (0xff8b8b89), c.getX(), c.getBottom(), false);
    g.setGradientFill (b); g.fillRoundedRectangle (c, 2.2f);
    for (float f : { 0.11f, 0.22f, 0.31f, 0.40f, 0.60f, 0.70f, 0.79f, 0.89f })
    {
        const float ly = c.getY() + f * ch;
        g.setColour (Colour (0xffe3e3df).withAlpha (0.9f)); g.fillRect (RF (c.getX() + 0.8f, ly, w - 1.6f, 0.9f));
        g.setColour (Colour (0xff66676a)); g.fillRect (RF (c.getX() + 0.8f, ly + 0.9f, w - 1.6f, 0.6f));
    }
    g.setColour (Colour (0xff1c1c1d)); g.fillRoundedRectangle (c.getX() - 0.4f, y - 0.9f, w + 0.8f, 1.8f, 0.6f);
}

// ---- Band type: one key the size of the 4K's BELL key, reading the current mode
static void bandType (Graphics& g, float cx, float cy, int selected)
{
    const char* names[] = { "BELL", "SHELF", "CUT" };
    RF r (cx - 19.0f, cy - 11.0f, 38.0f, 22.0f);
    key (g, r, selected == 2 ? String() : String (names[selected]), false, 9.4f);
    if (selected == 2) { const auto face = r.withTrimmedBottom (3.6f); text (g, names[2], face.getCentreX(), face.getCentreY() + 0.2f, sans (9.4f, true).withHorizontalScale (0.93f), Colour (0xff9a2a24)); }
}

static void rule (Graphics& g, std::initializer_list<Point<float>> pts)
{
    Path p; bool first = true; for (auto q : pts) { if (first) p.startNewSubPath (q); else p.lineTo (q); first = false; }
    g.setColour (P->rule); g.strokePath (p.createPathWithRoundedCorners (5.0f), juce::PathStrokeType (1.0f));
}
static void noise (Graphics& g, RF r, float alpha)
{
    static juce::Image tile = []
    {
        juce::Image im (juce::Image::ARGB, 128, 128, true); juce::Random rnd (7);
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) { const auto v = (juce::uint8) rnd.nextInt (2) * 255; im.setPixelAt (x, y, Colour::fromRGBA (v, v, v, (juce::uint8) rnd.nextInt (22))); }
        return im;
    }();
    g.setTiledImageFill (tile, 0, 0, alpha); g.fillRect (r);
}

// ---- Panel
struct Tones { Colour hf, hmf, lmf, lf, greyCap, level; };

static void paint (Graphics& g)
{
    const float W = 720.0f, mainH = 634.0f, H = 666.0f;
    const bool dark = P->darkStrip;
    const Tones t = dark ? Tones { Colour (0xff254175), Colour (0xff223d6d), Colour (0xff223d6d), Colour (0xff213c6c), Colour (0xffb7b7b6), Colour (0xff254175) }
                         : Tones { Colour (0xff7c1b1a), Colour (0xff1c422a), Colour (0xff2c6793), Colour (0xff4f3531), Colour (0xffbcbcba), Colour (0xff2c6793) };
    // Columns
    for (float x : { 0.0f, 540.0f })
    {
        ColourGradient bg (P->panelTop, 0.0f, 0.0f, P->panel, 0.0f, 300.0f, false);
        g.setGradientFill (bg); g.fillRect (RF (x, 0.0f, 180.0f, mainH));
    }
    g.setColour (P->meterCol); g.fillRect (RF (180.0f, 39.0f, 360.0f, mainH - 39.0f));
    { ColourGradient h (P->header.brighter (0.08f), 0.0f, 0.0f, P->header.darker (0.12f), 0.0f, 39.0f, false); g.setGradientFill (h); g.fillRect (RF (180.0f, 0.0f, 360.0f, 39.0f)); }
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRect (RF (180.0f, 38.5f, 360.0f, 1.0f));
    g.setColour (P->block); g.fillRect (RF (270.0f, 39.0f, 180.0f, 167.0f));
    { ColourGradient s (P->stripTop, 0.0f, 207.0f, P->stripBot, 0.0f, mainH, false); g.setGradientFill (s); g.fillRect (RF (270.0f, 207.0f, 180.0f, mainH - 207.0f)); }
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRect (RF (270.0f, 206.0f, 180.0f, 1.2f));
    for (float x : { 180.0f, 270.0f, 450.0f, 540.0f }) { g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRect (RF (x - 0.5f, 39.0f, 1.0f, mainH - 39.0f)); }
    noise (g, RF (0.0f, 0.0f, W, mainH), 0.16f);

    // ---- Header
    key (g, RF (197.0f, 9.0f, 36.0f, 20.0f), {}, true);
    { const float cx = 215.0f, cy = 19.0f, r = 4.6f; Path p; p.addCentredArc (cx, cy + 0.4f, r, r, 0.0f, 0.62f, juce::MathConstants<float>::twoPi - 0.62f, true); p.startNewSubPath (cx, cy - r - 1.1f); p.lineTo (cx, cy - 0.8f);
      g.setColour (Colour (0xffb65a1e)); g.strokePath (p, juce::PathStrokeType (1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    { RF d (248.0f, 8.5f, 223.0f, 21.0f); lcdPanel (g, d); lcdText (g, "COHERENCE", d.getX() + 8.0f, d.getCentreY() + 0.2f, 1.42f, Colour (0xff39bd63));
      if (msOn) { const String ch = sideSel ? "SIDE" : "MID"; lcdText (g, ch, d.getCentreX() + 2.0f, d.getCentreY() + 0.2f, 1.1f, sideSel ? Colour (0xfff0a830) : Colour (0xff39bd63)); }
      const String m = "WEISS"; const float mw = lcdWidth (m, 1.1f); lcdText (g, m, d.getRight() - mw - 8.0f, d.getCentreY() + 0.2f, 1.1f, Colour (0xff2a9149)); }
    key (g, RF (487.0f, 7.0f, 36.0f, 21.0f), "SETUP", false, 8.2f);

    // ---- Left: EQ
    const auto gainScale = linear (-15.0f, 15.0f, { { -15.f, "-15" }, { -12.f, "12" }, { -9.f, "9" }, { -6.f, "6" }, { -3.f, "3" }, { 0.f, "0" }, { 3.f, "3" }, { 6.f, "6" }, { 9.f, "9" }, { 12.f, "12" }, { 15.f, "+15" } });
    const auto qScale = logm (0.3f, 10.0f, { { .3f, ".3" }, { .5f, ".5" }, { 1.f, "1" }, { 2.f, "2" }, { 4.f, "4" }, { 10.f, "10" } });
    const auto hfScale = logm (1.f, 20.f, { { 1.f, "1" }, { 1.5f, "1.5" }, { 2.f, "2" }, { 3.f, "" }, { 5.f, "5" }, { 8.f, "8" }, { 12.f, "12" }, { 16.f, "" }, { 20.f, "20" } });
    const auto hmfScale = logm (.3f, 16.f, { { .3f, ".3" }, { .5f, ".5" }, { 1.f, "1" }, { 2.f, "2" }, { 4.f, "4" }, { 8.f, "8" }, { 16.f, "16" } });
    const auto lmfScale = logm (40.f, 4000.f, { { 40.f, "40" }, { 100.f, "100" }, { 200.f, "200" }, { 500.f, "500" }, { 1000.f, "1k" }, { 2000.f, "2k" }, { 4000.f, "4k" } });
    const auto lfScale = logm (16.f, 1000.f, { { 16.f, "16" }, { 30.f, "30" }, { 60.f, "60" }, { 120.f, "120" }, { 250.f, "250" }, { 500.f, "500" }, { 1000.f, "1k" } });
    const auto logN = [] (float v, float lo, float hi) { return std::log (v / lo) / std::log (hi / lo); };
    const auto gN = [] (float db) { return (db + 15.0f) / 30.0f; };
    const auto freqCap = [&] (Colour c) { return dark ? t.greyCap : c; };
    const auto bandLabel = [&] (const String& s, float x, float y, float h = 10.6f) { text (g, s, x, y, sans (h), P->ink, Justification::centredLeft); };

    knob (g, 49.0f, 45.0f, t.greyCap, logN (0.707f, 0.3f, 10.f), qScale, "HF Q", true);
    knob (g, 129.0f, 81.0f, t.greyCap, logN (0.707f, 0.3f, 10.f), qScale, "LF Q", true);
    text (g, "SHAPE", 5.0f, 123.0f, sans (13.2f), P->ink, Justification::centredLeft);
    rule (g, { { 4.0f, 135.0f }, { 176.0f, 135.0f } });
    bandLabel ("HF", 6.0f, 212.0f);
    knob (g, 49.0f, 170.0f, t.hf, gN (1.5f), gainScale, "dB");
    bandType (g, 129.0f, 157.5f, 1);
    knob (g, 129.0f, 211.0f, freqCap (t.hf), logN (12.f, 1.f, 20.f), hfScale, "kHz", dark);
    rule (g, { { 7.0f, 223.5f }, { 72.0f, 223.5f }, { 102.0f, 254.0f }, { 173.0f, 254.0f } });
    bandLabel ("HMF", 7.0f, 304.0f);
    knob (g, 49.0f, 260.0f, t.hmf, gN (-2.0f), gainScale, "dB");
    knob (g, 129.0f, 298.0f, freqCap (t.hmf), logN (3.15f, .3f, 16.f), hmfScale, "kHz", dark);
    knob (g, 49.0f, 334.0f, freqCap (t.hmf), logN (1.8f, .3f, 10.f), qScale, "Q", dark);

    text (g, "EQ", 5.0f, 380.0f, sans (15.5f), P->ink, Justification::centredLeft);
    rule (g, { { 31.0f, 380.0f }, { 88.0f, 380.0f } });
    key (g, RF (93.0f, 364.0f, 21.0f, 36.0f), "IN", true, 9.4f);
    key (g, RF (120.0f, 364.0f, 22.0f, 36.0f), "M/S", msOn, 7.6f);
    // Mid/Side rocker
    { RF r (148.0f, 361.0f, 21.0f, 42.0f);
      g.setColour (Colour (0xff131417).withAlpha (0.9f)); g.fillRoundedRectangle (r.expanded (1.4f), 3.0f);
      key (g, r.withHeight (20.5f), "MID", msOn && ! sideSel, 6.9f); key (g, r.withTrimmedTop (21.5f), "SIDE", msOn && sideSel, 6.9f); }

    bandLabel ("LMF", 6.0f, 464.0f);
    knob (g, 49.0f, 421.0f, t.lmf, gN (1.0f), gainScale, "dB");
    knob (g, 129.0f, 458.0f, freqCap (t.lmf), logN (240.f, 40.f, 4000.f), lmfScale, "Hz", dark);
    knob (g, 49.0f, 495.0f, freqCap (t.lmf), logN (0.8f, .3f, 10.f), qScale, "Q", dark);
    rule (g, { { 7.0f, 542.0f }, { 72.0f, 542.0f }, { 102.0f, 511.5f }, { 173.0f, 511.5f } });
    bandLabel ("LF", 6.0f, 561.0f);
    knob (g, 129.0f, 548.0f, freqCap (t.lf), logN (60.f, 16.f, 1000.f), lfScale, "Hz", dark);
    knob (g, 49.0f, 586.0f, t.lf, gN (3.0f), gainScale, "dB");
    bandType (g, 129.0f, 606.5f, 1);

    // ---- Centre
    meter (g, 180.0f, true, "INPUT", 0.40f, 0.42f, 0.27f, 0.30f, 0.38f);
    meter (g, 450.0f, false, "OUTPUT", 0.42f, 0.44f, 0.30f, 0.32f, 0.40f);

    const auto trimScale = linear (-24.f, 24.f, { { -24.f, "-24" }, { -18.f, "" }, { -12.f, "12" }, { -6.f, "" }, { 0.f, "0" }, { 6.f, "" }, { 12.f, "12" }, { 18.f, "" }, { 24.f, "+24" } });
    const auto balScale = linear (-100.f, 100.f, { { -100.f, "L" }, { -60.f, "" }, { -20.f, "" }, { 0.f, "C" }, { 20.f, "" }, { 60.f, "" }, { 100.f, "R" } });
    knob (g, 325.0f, 86.0f, t.greyCap, (1.0f + 24.0f) / 48.0f, trimScale, "IN TRIM", true);
    knob (g, 395.0f, 86.0f, t.greyCap, 0.5f, balScale, "BALANCE", true);
    key (g, RF (331.0f, 148.0f, 58.0f, 30.0f), "MONO", false, 11.0f);

    const auto levelScale = linear (-12.f, 12.f, { { -12.f, "-12" }, { -6.f, "6" }, { 0.f, "0" }, { 6.f, "6" }, { 12.f, "+12" }, { -9.f, "" }, { -3.f, "" }, { 3.f, "" }, { 9.f, "" } });
    const Colour ink = P->stripInk;
    const Colour levelCap = dark ? t.level : Colour (0xff2c6793);
    knob (g, 325.0f, 247.0f, levelCap, (-0.5f + 12.0f) / 24.0f, levelScale, "MID", false, 35.0f, ink.withAlpha (0.9f), ink);
    knob (g, 395.0f, 247.0f, levelCap, (1.5f + 12.0f) / 24.0f, levelScale, "SIDE", false, 35.0f, ink.withAlpha (0.9f), ink);
    key (g, RF (346.0f, 277.0f, 28.0f, 16.0f), "LINK", false, 7.4f);

    // M/S meter housing, in the place and manner of the channel switches
    { RF o (300.0f, 304.0f, 50.0f, 268.0f);
      g.setColour (Colour (0xff232324)); g.fillRoundedRectangle (o, 1.2f);
      g.setColour (Colour (0xff404041)); g.drawRoundedRectangle (o.reduced (0.3f), 1.2f, 0.6f);
      RF in = o.reduced (5.0f); g.setColour (Colour (0xff0f1011)); g.fillRect (in);
      RF m (in.getX() + 8.0f, in.getY() + 14.0f, 11.0f, in.getHeight() - 22.0f), s = m.translated (14.0f, 0.0f);
      bar (g, m, 0.46f, 0.40f); bar (g, s, 0.66f, 0.60f);
      text (g, "M", m.getCentreX(), in.getY() + 7.0f, sans (8.4f, true), Colour (0xff8a8b8e));
      text (g, "S", s.getCentreX(), in.getY() + 7.0f, sans (8.4f, true), Colour (0xff8a8b8e)); }
    fader (g, 395.0f, 303.0f, 612.0f, -1.5f, ink.withAlpha (0.92f));
    text (g, "EQ OUTPUT", 325.0f, 587.5f, sans (8.9f), ink);
    { RF d (299.0f, 595.0f, 52.0f, 24.0f); lcdPanel (g, d); const String v = "0.0 DB"; lcdText (g, v, d.getCentreX() - lcdWidth (v, 1.08f) * 0.5f, d.getCentreY(), 1.08f, Colour (0xff39bd63)); }

    // ---- Right: compressor
    text (g, "COMPRESS", 547.0f, 19.0f, sans (11.2f), P->ink, Justification::centredLeft);
    { RF d (618.0f, 8.5f, 62.0f, 21.0f); lcdPanel (g, d); const String v = "85.0%"; lcdText (g, v, d.getCentreX() - lcdWidth (v, 1.25f) * 0.5f, d.getCentreY(), 1.25f, Colour (0xff39bd63)); }
    text (g, "MIX", 702.0f, 19.0f, sans (11.2f), P->ink);
    const auto ratioPos = [] (float r) { return r >= 1000.f ? 1.0f : std::pow (1.0f - 1.0f / r, 1.25f); };
    std::vector<Mark> ratioScale; for (auto [r, l] : std::initializer_list<std::pair<float, const char*>> { { 1.f, "1" }, { 1.5f, "" }, { 2.f, "2" }, { 3.f, "" }, { 4.f, "4" }, { 8.f, "8" }, { 20.f, "20" }, { 1000.f, "oo" } }) ratioScale.push_back ({ ratioPos (r), r >= 1000.f ? String::fromUTF8 ("\xe2\x88\x9e") : String (l) });
    const auto threshScale = linear (-60.f, 0.f, { { -60.f, "-60" }, { -50.f, "" }, { -40.f, "40" }, { -30.f, "30" }, { -20.f, "20" }, { -10.f, "10" }, { 0.f, "0" } });
    const auto attackScale = logm (.05f, 150.f, { { .05f, ".05" }, { .2f, ".2" }, { 1.f, "1" }, { 5.f, "5" }, { 20.f, "20" }, { 150.f, "150" } });
    const auto releaseScale = logm (5.f, 3000.f, { { 5.f, "5" }, { 20.f, "20" }, { 50.f, "50" }, { 150.f, "150" }, { 500.f, "500" }, { 3000.f, "3k" } });
    std::vector<Mark> kneeScale; for (auto [k, l] : std::initializer_list<std::pair<float, const char*>> { { 0.f, "0" }, { 1.f, "1" }, { 4.f, "4" }, { 8.f, "8" }, { 20.f, "20" }, { 40.f, "40" }, { 72.f, "72" } }) kneeScale.push_back ({ std::pow (k / 72.0f, 1.0f / 2.4f), l });
    const auto scScale = logm (20.f, 500.f, { { 20.f, "OFF" }, { 30.f, "30" }, { 50.f, "50" }, { 100.f, "100" }, { 200.f, "200" }, { 500.f, "500" } });
    const auto makeupScale = trimScale;
    const auto smallCaption = [&] (const String& s, float keyX, float y) { text (g, s, keyX - 6.0f, y, sans (8.6f), P->ink, Justification::centredRight); };
    smallCaption ("AUTO RELEASE", 685.0f, 49.0f); key (g, RF (685.0f, 41.5f, 21.0f, 15.0f), {}, false);
    smallCaption ("AUTO MAKEUP", 685.0f, 71.0f); key (g, RF (685.0f, 63.5f, 21.0f, 15.0f), {}, true);
    knob (g, 591.0f, 86.0f, t.greyCap, ratioPos (3.0f), ratioScale, "RATIO", true);
    knob (g, 668.0f, 128.0f, t.greyCap, (-22.f + 60.f) / 60.f, threshScale, "THRESHOLD", true);
    knob (g, 591.0f, 180.0f, t.greyCap, logN (12.f, .05f, 150.f), attackScale, "ATTACK", true);

    text (g, "DYN", 549.0f, 253.0f, sans (15.5f), P->ink, Justification::centredLeft);
    key (g, RF (583.0f, 235.0f, 21.0f, 36.0f), "IN", true, 9.4f);
    rule (g, { { 610.0f, 253.0f }, { 640.0f, 253.0f } });
    { // the gain reduction ladder and its bracket
      const float x = 664.0f, y0 = 219.0f, dy = 17.0f; const char* gr[] = { "20", "14", "10", "6", "3" };
      for (int i = 0; i < 5; ++i)
      {
          const Colour c = i < 2 ? Colour (0xffe03a4a) : Colour (0xfff0b324);
          led (g, x - 10.0f, y0 + (float) i * dy, 4.6f, c, i >= 3);
          text (g, gr[i], x + 9.0f, y0 + (float) i * dy, sans (9.8f), P->num);
      }
      Path b; b.startNewSubPath (646.0f, 208.5f); b.lineTo (640.0f, 208.5f); b.lineTo (640.0f, 297.5f); b.lineTo (646.0f, 297.5f);
      g.setColour (P->rule); g.strokePath (b.createPathWithRoundedCorners (3.0f), juce::PathStrokeType (1.0f));
    }

    const auto dkCap = dark ? Colour (0xff3a3b3e) : Colour (0xff36373a);
    knob (g, 591.0f, 316.0f, dkCap, logN (180.f, 5.f, 3000.f), releaseScale, "RELEASE");
    knob (g, 668.0f, 344.0f, dkCap, std::pow (8.0f / 72.0f, 1.0f / 2.4f), kneeScale, "KNEE");
    knob (g, 591.0f, 396.0f, dkCap, logN (90.f, 20.f, 500.f), scScale, "SC HPF");
    knob (g, 668.0f, 424.0f, dkCap, (2.f + 24.f) / 48.f, makeupScale, "MAKEUP");

    // Brand plate
    { RF band (540.0f, 469.0f, 180.0f, 26.0f);
      ColourGradient s (P->plateTop, 0.0f, band.getY(), P->plateBot, 0.0f, band.getBottom(), false); g.setGradientFill (s); g.fillRect (band);
      RF plate = band.reduced (4.0f, 3.5f); g.setColour (Colour (0xff2b2b2d)); g.drawRoundedRectangle (plate, 3.0f, 1.0f);
      { Path a, b; const float x0 = plate.getX() + 7.0f, y0 = plate.getCentreY(), w = 15.0f, amp = 3.0f;
        for (int i = 0; i <= 40; ++i) { const float u = (float) i / 40.0f, ph = u * juce::MathConstants<float>::twoPi;
          const float y1 = y0 + std::sin (ph) * amp, y2 = y0 + std::sin (ph - juce::MathConstants<float>::halfPi) * amp;
          if (i == 0) { a.startNewSubPath (x0, y1); b.startNewSubPath (x0, y2); } else { a.lineTo (x0 + u * w, y1); b.lineTo (x0 + u * w, y2); } }
        g.setColour (Colour (0xff232325)); g.strokePath (a, juce::PathStrokeType (1.0f)); g.setColour (Colour (0xff1f4f86)); g.strokePath (b, juce::PathStrokeType (1.0f)); }
      text (g, String::fromUTF8 ("PHASE SYSTEMS \xe2\x80\xa2 COHERENCE"), plate.getCentreX() + 8.0f, plate.getCentreY() + 0.2f, sans (9.6f, false, 0.02f), Colour (0xff232325)); }
    // Signal order screen
    { RF d (550.0f, 504.0f, 160.0f, 121.0f); g.setColour (Colour (0xff0c0d0f)); g.fillRoundedRectangle (d.expanded (1.6f), 6.0f);
      ColourGradient bg (Colour (0xff17331f), d.getCentreX(), d.getCentreY(), Colour (0xff081009), d.getX(), d.getY(), true); g.setGradientFill (bg); g.fillRoundedRectangle (d, 5.0f);
      const Colour lit (0xff39bd63), dim (0xff237a3c);
      lcdText (g, "SIGNAL ORDER", d.getCentreX() - lcdWidth ("SIGNAL ORDER", 0.95f) * 0.5f, d.getY() + 13.0f, 0.95f, dim);
      const float bx = d.getCentreX() - 34.0f, bw = 68.0f; const char* blocks[] = { "EQ", "DYN", "M/S" };
      for (int i = 0; i < 3; ++i)
      {
          RF b (bx, d.getY() + 25.0f + (float) i * 22.0f, bw, 16.0f); g.setColour (Colour (0xff133d21)); g.fillRoundedRectangle (b, 2.0f);
          lcdText (g, blocks[i], b.getCentreX() - lcdWidth (blocks[i], 1.02f) * 0.5f, b.getCentreY(), 1.02f, lit);
          if (i < 2) { g.setColour (dim); g.fillRect (RF (b.getCentreX() - 0.5f, b.getBottom(), 1.0f, 6.0f)); }
      }
      g.setColour (lit); Path a; const float ax = d.getCentreX(), ay = d.getBottom() - 21.0f; a.addRectangle (ax - 1.2f, ay, 2.4f, 8.0f); a.addTriangle (ax - 5.0f, ay + 8.0f, ax + 5.0f, ay + 8.0f, ax, ay + 14.0f); g.fillPath (a); }

    // ---- Bottom bar
    { ColourGradient b (P->barTop, 0.0f, mainH, P->barBot, 0.0f, H, false); g.setGradientFill (b); g.fillRect (RF (0.0f, mainH, W, H - mainH));
      g.setColour (juce::Colours::black); g.fillRect (RF (0.0f, mainH, W, 1.0f));
      for (float x : { 180.0f, 540.0f }) { g.setColour (Colour (0xff05060a)); g.fillRect (RF (x - 0.5f, mainH, 1.0f, H - mainH)); }
      const Colour dimc (0xff5a5b5f);
      const auto arrow = [&] (float cx, bool flip)
      {
          juce::Path p; const float cy = 650.5f; p.startNewSubPath (cx + 6.5f, cy + 5.0f); p.lineTo (cx + 6.5f, cy + 0.5f); p.quadraticTo (cx + 6.5f, cy - 3.0f, cx + 2.0f, cy - 3.0f); p.lineTo (cx - 6.0f, cy - 3.0f);
          p.startNewSubPath (cx - 2.4f, cy - 6.6f); p.lineTo (cx - 6.0f, cy - 3.0f); p.lineTo (cx - 2.4f, cy + 0.6f);
          if (flip) p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, cx, cy));
          g.setColour (dimc); g.strokePath (p, juce::PathStrokeType (1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
      };
      arrow (21.0f, false); arrow (52.0f, true);
      text (g, "COPY", 90.0f, 651.0f, sans (8.8f), dimc);
      text (g, "A", 131.0f, 650.5f, sans (15.0f), onB ? dimc : Colour (0xff35d873));
      text (g, "B", 163.0f, 650.5f, sans (15.0f), onB ? Colour (0xff35d873) : dimc);
      const String status = "HF GAIN  +1.5 DB"; lcdText (g, status, 360.0f - lcdWidth (status, 1.3f) * 0.5f, 650.5f, 1.3f, Colour (0xff39bd63));
      text (g, "v1.0", 561.0f, 651.0f, sans (9.2f), dimc);
      text (g, "48 kHz   15 smp", 708.0f, 651.0f, sans (9.2f), dimc, Justification::centredRight); }
}

static const pal::P* byName (const juce::String& n)
{
    if (n == "graphite") return &pal::graphite;
    if (n == "champagne") return &pal::champagne;
    if (n == "b") return &pal::b;
    return &pal::e;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out (argv[1]);
    const float s = argc > 2 ? juce::String (argv[2]).getFloatValue() : 2.0f;
    juce::Image im;
    if (argc > 3 && juce::String (argv[3]) == "sheet")
    {
        // All themes at identical dimensions, with the same control values.
        const struct { const pal::P* p; const char* name; } themes[] = { { &pal::e, "CLASSIC" }, { &pal::graphite, "WARM GRAPHITE" }, { &pal::champagne, "CHAMPAGNE" } };
        const int w = juce::roundToInt (720.0f * s), h = juce::roundToInt (666.0f * s), gap = 28, title = 54;
        im = juce::Image (juce::Image::ARGB, w * 3 + gap * 4, h + title + gap, true);
        Graphics g (im);
        g.fillAll (Colour (0xff101012));
        for (int i = 0; i < 3; ++i)
        {
            const int x = gap + i * (w + gap);
            g.setFont (sans (24.0f)); g.setColour (Colour (0xffd8d6d0));
            g.drawText (themes[i].name, juce::Rectangle<int> (x, 0, w, title), Justification::centred, false);
            P = themes[i].p;
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (x, title, w, h);
            g.addTransform (juce::AffineTransform::scale (s).translated ((float) x, (float) title));
            paint (g);
        }
    }
    else
    {
        P = byName (argc > 3 ? juce::String (argv[3]) : juce::String ("e"));
        if (argc > 4 && juce::String (argv[4]) == "side") { msOn = true; sideSel = true; onB = true; }
        im = juce::Image (juce::Image::ARGB, juce::roundToInt (720.0f * s), juce::roundToInt (666.0f * s), true);
        Graphics g (im); g.addTransform (juce::AffineTransform::scale (s)); paint (g);
    }
    juce::File f (out); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (im, os);
    return 0;
}
