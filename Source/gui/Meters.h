// COHERENCE - meters. Values are pushed in by the editor's timer; ballistics are applied here.

#pragma once

#include "Controls.h"

namespace coherence::gui
{
struct Ballistics
{
    float displayDb = -120.0f;   // peak bar
    float holdDb = -120.0f;      // peak-hold tick
    float holdTime = 0.0f;
    void push (float peakDb, float dt);
};

// Two channel bars and the narrow hold bar. The scale and title are printed on the panel behind it.
class LevelMeter final : public juce::Component
{
public:
    enum class ScaleSide { left, right };
    LevelMeter (const juce::String& title, ScaleSide, HoverSink* = nullptr);

    void update (const float* peakLin, const float* rmsLin, int numChannels, float dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;   // resets the max readout
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr float top = 73.0f, bottom = 618.0f;   // panel coordinates of the bars
    static float yOfDb (float db) { return 76.5f + juce::jlimit (-0.4f, 60.4f, -db) * 9.0f; }

    float maxPeak() const { return maxPeakDb; }

private:
    juce::String title;
    ScaleSide side;
    HoverSink* hover;
    std::array<Ballistics, 2> ch;
    std::array<float, 2> rmsDb { -120.0f, -120.0f };
    float maxPeakDb = -120.0f;
    int channels = 2;
};

// The gain reduction ladder: 20, 14, 10, 6 and 3 dB.
class GainReductionMeter final : public juce::Component
{
public:
    explicit GainReductionMeter (HoverSink* = nullptr);
    void update (float grDb, float dt);
    void paint (juce::Graphics&) override;
    void setActive (bool a) { if (a != active) { active = a; repaint(); } }
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    HoverSink* hover;
    float display = 0.0f;   // <= 0
    bool active = true;
};

// Mid and Side levels, in the housing where the 4K has its channel switches.
class MidSideMeter final : public juce::Component
{
public:
    explicit MidSideMeter (HoverSink* = nullptr);
    void update (float midLin, float sideLin, bool stereo, float dt);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    HoverSink* hover;
    float midDb = -120.0f, sideDb = -120.0f;
    bool isStereo = true;
};
} // namespace coherence::gui
