// COHERENCE - signal processing engine (plain C++, no JUCE dependency).
//
// Signal flow (per channel pair, double precision throughout):
//
//   input trim -> [ EQ -> DYN ]  or  [ DYN -> EQ ] -> architecture (M/S, balance) -> auto gain -> output fader
//                                                                                   -> mono (monitoring only)
//
// Everything is processed in sub-blocks of `subBlock` samples. Parameter smoothing runs once per sub-block and
// every gain or coefficient is ramped linearly across the sub-block, so automation is zipper-free.
// No memory is allocated after prepare().

#pragma once

#include "EqDesign.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <atomic>

namespace coherence::dsp
{
constexpr int maxChannels = 2;
constexpr int subBlock = 32;
constexpr int numBands = 4;

//==============================================================================
struct BandSettings
{
    Shape shape = Shape::bell;
    double frequency = 1000.0;
    double gainDb = 0.0;
    double q = 0.71;
};

struct CompSettings
{
    double thresholdDb = -18.0;
    double ratio = 4.0;            // >= 1000 treated as infinity
    double attackMs = 10.0;
    double releaseMs = 150.0;
    bool autoRelease = false;
    double knee = 0.4;             // 0..1, the knee width is kneeWidthDb (knee): 0..72 dB (0.4 = 8 dB), centred on the threshold
    double makeupDb = 0.0;
    bool autoMakeup = false;
    double mix = 1.0;              // 0..1
    double sidechainHpfHz = 0.0;   // 0 = off
};

struct Settings
{
    bool bypass = false;
    double inputDb = 0.0;

    bool eqOn = true;
    bool weissEq = false;          // Weiss mode: the EQ at 2x (DESIGN.md section 15), latency weissLatencySamples
    std::array<BandSettings, numBands> bands {};   // stereo; the Mid channel in M/S
    bool msEq = false;             // M/S EQ: Mid and Side processed independently (stereo tracks only)
    std::array<BandSettings, numBands> sideBands {};

    bool dynOn = false;
    CompSettings comp {};

    bool compFirst = false;        // false: EQ -> DYN, true: DYN -> EQ

    double midDb = 0.0;
    double sideDb = 0.0;
    double balance = 0.0;          // -1 (left) .. +1 (right)
    bool mono = false;

    double outputDb = 0.0;         // <= outputOffDb means fully off

    double eqOutputDb = 0.0;        // level matching after EQ moves: after the compressor; follows eqOn
};

constexpr double outputOffDb = -60.0;
// Input samples beyond this (+120 dBFS), NaN and Inf are taken as 0: no audio gets there (DESIGN.md section 21).
constexpr float maxInputSample = 1.0e6f;
constexpr double kneeMaxDb = 72.0;
// Knee control (0..1) to knee width in dB: 72 * v^2.4, so 0.4 (the default) is 8.0 dB, as in v0.2.3, and 1 is 72 dB.
inline double kneeWidthDb (double v) noexcept { return kneeMaxDb * std::pow (std::clamp (v, 0.0, 1.0), 2.4); }

//==============================================================================
// Meter values shared with the editor. The engine only writes; the editor reads (and resets peaks).
struct MeterData
{
    std::array<std::atomic<float>, 2> inPeak {}, outPeak {};   // linear, max since last read
    std::array<std::atomic<float>, 2> inRms {}, outRms {};     // linear RMS, 300 ms
    std::atomic<float> midRms { 0.0f }, sideRms { 0.0f };       // of the output, 300 ms
    std::atomic<float> gainReductionDb { 0.0f };                // most negative since last read (<= 0)
    std::atomic<float> hostGainReductionDb { 0.0f };            // the same, read by the processor for the host meter
    std::atomic<int> numChannels { 2 };

    static void storeMax (std::atomic<float>& a, float v)
    {
        auto cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
    static void storeMin (std::atomic<float>& a, float v)
    {
        auto cur = a.load (std::memory_order_relaxed);
        while (v < cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
};

//==============================================================================
struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;
};

inline double svfTick (const SvfCoeffs& c, SvfState& s, double x) noexcept
{
    const double v3 = x - s.ic2;
    const double v1 = c.a1 * s.ic1 + c.a2 * v3;
    const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0 * v1 - s.ic1;
    s.ic2 = 2.0 * v2 - s.ic2;
    return c.m0 * x + c.m1 * v1 + c.m2 * v2;
}

// Linear ramp of a value across one sub-block.
struct Ramp
{
    double value = 0.0, step = 0.0;
    double next() noexcept { const double v = value; value += step; return v; }
};

//==============================================================================
class Equaliser
{
public:
    void prepare (double sampleRate);
    void reset();
    void setTargets (const std::array<BandSettings, numBands>&, bool on);
    void process (double* const* ch, int numChannels, int n) noexcept;

    // Weiss mode: the bands run at twice the rate between a half-band interpolator and decimation. Switching
    // resets the filters (the engine fades around it). Off = the normal zero-latency EQ, unchanged.
    void setWeissMode (bool);
    bool weissMode() const { return weiss; }

    // The coefficients currently in use (for measurement and display).
    SvfCoeffs currentCoeffs (int band) const { return bands[(size_t) band].cur; }

private:
    struct Band
    {
        Shape shape = Shape::bell;
        double logF = 0.0, gainDb = 0.0, logQ = 0.0;      // smoothed
        double tLogF = 0.0, tGainDb = 0.0, tLogQ = 0.0;   // targets
        Shape tShape = Shape::bell;
        bool dirty = true;
        SvfCoeffs cur, step;
        std::array<SvfState, maxChannels> st {};
        // Shape change: the previous filter keeps running and is crossfaded out.
        SvfCoeffs old;
        std::array<SvfState, maxChannels> oldSt {};
        int fadeLeft = 0;
    };

    SvfCoeffs designFor (const Band&) const;
    void runBands (double* const* x, int numChannels, int len) noexcept;
    void processWeiss (double* const* ch, int numChannels, int n) noexcept;

    bool weiss = false;
    double bandRate = 48000.0;   // the rate the bands run at: fs, or 2 fs in Weiss mode
    static constexpr int hbHistory = 2 * weissHalfbandTaps - 1;   // input samples of history the interpolator needs
    std::array<std::array<double, hbHistory + subBlock>, maxChannels> hbIn {};

    double fs = 48000.0;
    double smoothCoeff = 0.0;   // per sub-block
    int fadeLength = 1;
    std::array<Band, numBands> bands {};
    double onTarget = 1.0, onValue = 1.0, onCoeff = 0.0;
    bool first = true;
};

//==============================================================================
// The compressor (v0.3.0): a model of FabFilter Pro-C 3's Clean style, measured from the plug-in
// (COMPRESSOR-TESTING.md, mac-results/proc3-clean/). Feed-forward; everything after the detector is in dB:
//   detector   stereo-linked instantaneous peak, max(|L|, |R|), after the optional sidechain high-pass;
//   target     gain reduction from the static curve: slope * h(level - threshold, knee), with Pro-C 3's measured
//              knee table (CompKneeTable.h);
//   attack     toward the target with a time that depends on the distance to it (large steps fast, small
//              corrections gentle): tau = kA * attack + c0 + t0 / (1 + (d / d0)^p);
//   release    a fast path (tau = 0.346 R + 5.5 ms, measured) and two slow stores that charge during sustained
//              reduction and then follow the fast path down (program-dependent release: the longer the reduction
//              lasted, the slower the recovery); the deepest applies. Long attack settings shorten the release.
class Compressor
{
public:
    void prepare (double sampleRate);
    void reset();
    void setTargets (const CompSettings&, bool on);
    void process (double* const* ch, int numChannels, int n) noexcept;

    float takeMinGainDb() noexcept { const auto v = minGainDb; minGainDb = 0.0f; return v; }

    // Static gain (dB, <= 0) for a detector level in dBFS, using the current smoothed settings.
    double staticGainDb (double levelDb) const noexcept;
    static double staticGainDb (double levelDb, double thresholdDb, double slope, double kneeDb) noexcept;

    // The model's constants (fitted to Pro-C 3 Clean; Tools/proc3/fit_dynamics.py).
    struct Model
    {
        double kA, c0, t0, d0, p;          // attack (ms; d0 in dB)
        double kF, cF, kD, qD, tMin;       // fast release (ms)
        double tc, a0c, qc;                // slow store charging (ms)
        double kS, cS, kDs, qDs;           // slow store release (ms)
        double tc2, kS2, cS2;              // second, slower store: charging and release (ms)
    };
    static const Model model;

    // Release time (ms) used with AUTO release: Pro-C 3's Auto Release behaves as this manual release.
    static double autoReleaseMs (double releaseMs) noexcept;

private:
    double fs = 48000.0;
    double sbCoeff = 0.0;      // parameter smoothing per sub-block

    // smoothed parameters (per sub-block) and their per-sample ramps
    double threshold = -18.0, tThreshold = -18.0;
    double slope = 0.75, tSlope = 0.75;
    double kneeDb = 8.0, tKneeDb = 8.0;
    double makeupDb = 0.0, tMakeupDb = 0.0;
    double mix = 1.0, tMix = 1.0;
    double on = 0.0, tOn = 0.0;
    Ramp makeupRamp, mixRamp, onRamp;

    // Dynamics state (gain reduction in dB, >= 0) and per-setting coefficients.
    double fast = 0.0, slow = 0.0, slow2 = 0.0;
    double attackBase = 0.0;               // kA * attack + c0, in samples
    double t0Samples = 0.0, invD0 = 1.0;
    double relFast = 0.0, relSlow = 0.0, charge = 0.0, relSlow2 = 0.0, charge2 = 0.0;
    double smooth (double target) noexcept;   // returns the gain reduction (dB, >= 0)

    // sidechain high-pass (Butterworth SVF)
    double hpfHz = 0.0, tHpfHz = 0.0;
    SvfCoeffs hpf;
    double hpfK = 1.41421356237;
    std::array<SvfState, maxChannels> hpfSt {};

    float minGainDb = 0.0f;
    bool first = true;

    // AUTO makeup: restores a fixed share of the level this compressor measurably takes away (DESIGN.md section 16).
    // It reads the compressor and never feeds back into it. It is not Pro-C 3's law, by the owner's decision.
    static constexpr int autoMakeupStep = 32;          // samples per update
    static constexpr double autoMakeupShare = 0.8;     // of the measured loss: stays under the input's loudness (mac-results/v0.5.0)
    void stepAutoMakeup() noexcept;
    double tManualMakeupDb = 0.0;
    bool autoOn = false;
    double autoIn = 0.0, autoOut = 0.0;    // slow mean power before and after the gain reduction
    double autoDb = 0.0;                   // the makeup, dB, >= 0
    double autoSumE = 0.0, autoSumEGr = 0.0, autoDeepestGr = 0.0;
    int autoCount = 0;
    int autoHoldLeft = 0, autoMoveLeft = 0, autoHoldSamples = 0, autoMoveSamples = 0;
    double autoCoeffSlow = 0.0, autoCoeffFall = 0.0, autoCoeffGlide = 0.0;
    std::array<double, 7> autoKnobs {};    // the settings that change the reduction
};

//==============================================================================
class Engine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    // In-place processing of float audio.
    void process (float* const* io, int numChannels, int numSamples, const Settings&, MeterData&) noexcept;

    double sampleRate() const { return fs; }


    // For tests: direct access to the processing slots.
    const Equaliser& equaliser (bool compFirst) const { return slots[compFirst ? 1 : 0].eq; }

    // The latency the engine currently has (Weiss mode: weissLatencySamples, otherwise 0).
    int latencySamples() const { return weissActive ? weissLatencySamples : 0; }

private:
    struct Slot
    {
        Equaliser eq;       // stereo; the Mid channel in M/S
        Equaliser eqSide;   // the Side channel in M/S; idle in stereo
        Compressor comp;
        bool compFirst = false;
        bool ms = false;
        void processEq (double* const* ch, int nCh, int n) noexcept;
        void process (double* const* ch, int nCh, int n) noexcept
        {
            if (compFirst) { comp.process (ch, nCh, n); processEq (ch, nCh, n); }
            else           { processEq (ch, nCh, n); comp.process (ch, nCh, n); }
        }
    };

    void processSubBlock (double* const* ch, int nCh, int n, const Settings&, MeterData&) noexcept;
    bool msActive = false, msPending = false;
    void applyTargets (const Settings&);

    double fs = 48000.0;
    int maxBlock = 0;
    std::array<Slot, 2> slots;
    int active = 0;
    int orderFadeLeft = 0, orderFadeLength = 1;

    // smoothed gains (per sub-block, ramped per sample)
    double sbCoeff = 0.0;
    double inGain = 1.0, tInGain = 1.0;
    double outGain = 1.0, tOutGain = 1.0;
    double midG = 1.0, tMidG = 1.0, sideG = 1.0, tSideG = 1.0;
    double balL = 1.0, tBalL = 1.0, balR = 1.0, tBalR = 1.0;
    double monoMix = 0.0, tMonoMix = 0.0;
    double bypassMix = 0.0, tBypassMix = 0.0;
    double eqOutGain = 1.0, tEqOutGain = 1.0;

    // meters
    double rmsCoeff = 0.0;
    std::array<double, maxChannels> inMs {}, outMs {};
    double midMs = 0.0, sideMs = 0.0;

    // Weiss mode: switched at a moment of silence (5 ms fade out and in); the bypass path is delayed to match.
    bool weissActive = false, weissPending = false;
    double modeGain = 1.0;
    int modeFadeSamples = 1;
    std::array<std::array<double, weissLatencySamples + subBlock>, maxChannels> dryDelay {};

    bool first = true;
};
} // namespace coherence::dsp
