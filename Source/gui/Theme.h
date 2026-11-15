// COHERENCE - the panel's appearance: themes, and the shared drawing routines (knobs, keys, LEDs, meters, readouts).
//
// A theme is one Palette, and nothing else. Controls, layout, parameters and audio don't know which theme is in use, so
// themes can't change what the panel does. To add a theme: write one function in Theme.cpp and add it to the table there.
// The choice belongs to the user (UserSettings), not to a set or preset.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace coherence::theme
{
struct Palette
{
    const char* id = "";       // saved in the user's settings, so it must never change
    const char* name = "";     // shown in the SETUP menu

    // Surfaces
    juce::Colour panelTop, panel, header, meterColumn, block, stripTop, stripBottom;
    juce::Colour plateTop, plateBottom, barTop, barBottom;
    // Printing
    juce::Colour ink, inkDim, stripInk, rule, dot, number, plateInk, barDim, abActive;
    // Knob caps
    juce::Colour capHf, capHmf, capLmf, capLf, capGrey, capDark, capLevel;
    // Meters and readouts
    juce::Colour meterUnlit, meterLitTop, meterLitBottom, meterPeak, meterCap;
    juce::Colour scaleHot, scaleCool, lcdLit, lcdDim, lcdAlert;
    float grain = 0.16f;
};

int numThemes();
const Palette& themeAt (int index);
const Palette& current();
// Selects a theme by its saved id. Anything unknown falls back to Classic, the first in the table.
void setCurrent (const juce::String& id);

// The window can be any size, so everything is drawn as vectors in panel coordinates (720 x 666).
juce::Font sans (float height, bool bold = false, float kerning = 0.0f);
void text (juce::Graphics&, const juce::String&, float x, float cy, const juce::Font&, juce::Colour,
           juce::Justification = juce::Justification::centred);

// Dot-matrix readouts. Text is upper case; lcdString() brings any parameter text to what the matrix can show.
juce::String lcdString (const juce::String&);
float lcdWidth (const juce::String&, float pitch);
void lcdText (juce::Graphics&, const juce::String&, float x, float cy, float pitch, juce::Colour);
void lcdPanel (juce::Graphics&, juce::Rectangle<float>);

// Knobs
struct Mark { float pos; juce::String label; };   // pos: normalised 0..1
enum class Cap { hf, hmf, lmf, lf, grey, dark, level };
struct KnobLook
{
    Cap cap = Cap::grey;
    std::vector<Mark> marks;
    bool onStrip = false;     // printed on the centre strip rather than on a dark panel
    bool enabled = true;
    float captionDy = 34.0f;
};
constexpr float knobHitRadius = 21.0f;
void drawKnob (juce::Graphics&, float cx, float cy, const KnobLook&, float normalised, const juce::String& caption);

// Keycaps
void drawKey (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, bool lit, float textHeight = 9.6f);
void drawKeyText (juce::Graphics&, juce::Rectangle<float>, const juce::String&, float textHeight, juce::Colour);

void drawLed (juce::Graphics&, float cx, float cy, float radius, juce::Colour, bool lit);
void drawMeterBar (juce::Graphics&, juce::Rectangle<float>, float rmsFraction, float peakFraction);   // fractions down from the top
void drawRule (juce::Graphics&, std::initializer_list<juce::Point<float>>);
void drawGrain (juce::Graphics&, juce::Rectangle<float>);

// The Phase Systems mark (two sine waves a quarter cycle apart).
void drawMark (juce::Graphics&, float x, float cy, float width, float amplitude);
} // namespace coherence::theme
