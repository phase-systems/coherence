#include "Engine.h"
#include "CompKneeTable.h"

#include <algorithm>
#include <cmath>

namespace coherence::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double ln10over20 = 2.302585092994046 / 20.0;

inline double dbToGain (double db) noexcept { return std::exp (db * ln10over20); }
inline double gainToDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1e-30)); }

// One-pole step of a smoothed value towards its target, with a snap when close enough.
inline double approach (double value, double target, double coeff, double snap) noexcept
{
    const double v = target + (value - target) * coeff;
    return std::abs (v - target) < snap ? target : v;
}

inline Ramp rampTo (double& value, double target, double coeff, double snap, int n) noexcept
{
    const double start = value;
    value = approach (value, target, coeff, snap);
    return { start, (value - start) / (double) n };
}

SvfCoeffs operator- (const SvfCoeffs& a, const SvfCoeffs& b)
{
    return { a.a1 - b.a1, a.a2 - b.a2, a.a3 - b.a3, a.m0 - b.m0, a.m1 - b.m1, a.m2 - b.m2 };
}
SvfCoeffs scaled (const SvfCoeffs& a, double s)
{
    return { a.a1 * s, a.a2 * s, a.a3 * s, a.m0 * s, a.m1 * s, a.m2 * s };
}
// Raised-cosine crossfade weight for t in [0, 1]: no corners at either end.
inline double smoothFade (double t) noexcept
{
    t = std::clamp (t, 0.0, 1.0);
    return 0.5 - 0.5 * std::cos (pi * t);
}

constexpr SvfCoeffs zeroCoeffs { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

inline void addTo (SvfCoeffs& a, const SvfCoeffs& d) noexcept
{
    a.a1 += d.a1; a.a2 += d.a2; a.a3 += d.a3; a.m0 += d.m0; a.m1 += d.m1; a.m2 += d.m2;
}
} // namespace

//==============================================================================
// Equaliser
void Equaliser::prepare (double sampleRate)
{
    fs = sampleRate;
    bandRate = weiss ? 2.0 * fs : fs;
    smoothCoeff = std::exp (-(double) subBlock / (0.012 * fs));   // 12 ms parameter glide
    onCoeff = std::exp (-(double) subBlock / (0.006 * fs));
    fadeLength = std::max (1, (int) std::lround (0.015 * bandRate));    // 15 ms shape-change crossfade
    reset();
}

void Equaliser::setWeissMode (bool w)
{
    if (w == weiss)
        return;
    weiss = w;
    bandRate = weiss ? 2.0 * fs : fs;
    fadeLength = std::max (1, (int) std::lround (0.015 * bandRate));
    for (auto& b : bands)
    {
        b.st = {};
        b.oldSt = {};
        b.fadeLeft = 0;
        b.shape = b.tShape;
        b.cur = designFor (b);
        b.step = zeroCoeffs;
        b.dirty = false;
    }
    for (auto& h : hbIn)
        h.fill (0.0);
}

void Equaliser::reset()
{
    for (auto& b : bands)
    {
        b.st = {};
        b.oldSt = {};
        b.fadeLeft = 0;
        b.dirty = true;
    }
    for (auto& h : hbIn)
        h.fill (0.0);
    first = true;
}

void Equaliser::setTargets (const std::array<BandSettings, numBands>& s, bool on)
{
    for (size_t i = 0; i < bands.size(); ++i)
    {
        auto& b = bands[i];
        b.tShape = s[i].shape;
        b.tLogF = std::log (std::max (1.0, s[i].frequency));
        b.tGainDb = (s[i].shape == Shape::lowCut || s[i].shape == Shape::highCut) ? 0.0 : s[i].gainDb;
        b.tLogQ = std::log (std::max (0.05, s[i].q));
        if (first)
        {
            b.shape = b.tShape;
            b.logF = b.tLogF;
            b.gainDb = b.tGainDb;
            b.logQ = b.tLogQ;
            b.cur = designFor (b);
            b.step = zeroCoeffs;
            b.dirty = false;
        }
    }
    onTarget = on ? 1.0 : 0.0;
    if (first)
        onValue = onTarget;
    first = false;
}

SvfCoeffs Equaliser::designFor (const Band& b) const
{
    if (weiss)
        return coeffsFromSvf (designWeiss (b.shape, std::exp (b.logF), b.gainDb, std::exp (b.logQ), bandRate));
    double q = effectiveQ (b.shape, std::exp (b.logQ));
    if (b.shape == Shape::bell)
        q = bellDesignQ (q, b.gainDb);   // panel Q (MDW / classic definition) -> design Q
    return coeffsFromSvf (designBand (b.shape, std::exp (b.logF), b.gainDb, q, fs));
}

void Equaliser::process (double* const* ch, int numChannels, int n) noexcept
{
    if (weiss)
    {
        processWeiss (ch, numChannels, n);
        return;
    }

    double dryBuf[maxChannels][subBlock];
    for (int c = 0; c < numChannels; ++c)
        std::copy (ch[c], ch[c] + n, dryBuf[c]);

    runBands (ch, numChannels, n);

    // EQ IN: crossfade between the processed and the dry signal (the filters keep running while out).
    const double start = onValue;
    onValue = approach (onValue, onTarget, onCoeff, 1e-5);
    if (start == 1.0 && onValue == 1.0)
        return;
    const double step = (onValue - start) / (double) n;
    for (int c = 0; c < numChannels; ++c)
    {
        double w = start;
        for (int i = 0; i < n; ++i, w += step)
            ch[c][i] = dryBuf[c][i] + w * (ch[c][i] - dryBuf[c][i]);
    }
}

void Equaliser::processWeiss (double* const* ch, int numChannels, int n) noexcept
{
    // Interpolate to 2x with the half-band (63 taps, linear phase), run the bands, keep every second sample.
    // Input sample m of this block sits at hbIn[c][hbHistory + m]; output m corresponds to input m - 15.
    constexpr int L = weissLatencySamples;
    double upBuf[maxChannels][2 * subBlock];
    double dryBuf[maxChannels][subBlock];
    double* up[maxChannels] = { upBuf[0], upBuf[1] };
    for (int c = 0; c < numChannels; ++c)
    {
        auto& h = hbIn[(size_t) c];
        std::copy (ch[c], ch[c] + n, h.begin() + hbHistory);
        for (int m = 0; m < n; ++m)
        {
            const double* x = h.data() + hbHistory + m;   // x[-d] = input m - d
            double acc = 0.0;
            for (int k = 0; k < weissHalfbandTaps; ++k)
                acc += weissHalfband[k] * (x[-(L + 1 + k)] + x[-(L - k)]);
            upBuf[c][2 * m] = acc;            // half a sample before input m - 15
            upBuf[c][2 * m + 1] = x[-L];      // input m - 15 (the half-band's centre tap is 1)
            dryBuf[c][m] = x[-L];
        }
        std::copy (h.begin() + n, h.begin() + n + hbHistory, h.begin());
    }

    runBands (up, numChannels, 2 * n);

    for (int c = 0; c < numChannels; ++c)
        for (int m = 0; m < n; ++m)
            ch[c][m] = upBuf[c][2 * m + 1];

    // EQ IN: as in the normal mode, against the dry signal delayed by the same 15 samples.
    const double start = onValue;
    onValue = approach (onValue, onTarget, onCoeff, 1e-5);
    if (start == 1.0 && onValue == 1.0)
        return;
    const double step = (onValue - start) / (double) n;
    for (int c = 0; c < numChannels; ++c)
    {
        double w = start;
        for (int i = 0; i < n; ++i, w += step)
            ch[c][i] = dryBuf[c][i] + w * (ch[c][i] - dryBuf[c][i]);
    }
}

void Equaliser::runBands (double* const* ch, int numChannels, int n) noexcept
{
    for (auto& b : bands)
    {
        if (b.tShape != b.shape)
        {
            // Keep the old filter running and crossfade to the new shape.
            b.old = b.cur;
            b.oldSt = b.st;
            b.fadeLeft = fadeLength;
            b.shape = b.tShape;
            b.cur = designFor (b);
            b.step = zeroCoeffs;
        }

        const double lf = b.logF, g = b.gainDb, lq = b.logQ;
        b.logF = approach (b.logF, b.tLogF, smoothCoeff, 1e-7);
        b.gainDb = approach (b.gainDb, b.tGainDb, smoothCoeff, 1e-6);
        b.logQ = approach (b.logQ, b.tLogQ, smoothCoeff, 1e-7);

        SvfCoeffs target = b.cur;
        if (b.dirty || lf != b.logF || g != b.gainDb || lq != b.logQ)
        {
            target = designFor (b);
            b.step = scaled (target - b.cur, 1.0 / (double) n);
            b.dirty = false;
        }
        else
        {
            b.step = zeroCoeffs;
        }

        const bool fading = b.fadeLeft > 0;
        for (int c = 0; c < numChannels; ++c)
        {
            auto* x = ch[c];
            auto k = b.cur;
            auto& st = b.st[(size_t) c];
            if (! fading)
            {
                for (int i = 0; i < n; ++i)
                {
                    x[i] = svfTick (k, st, x[i]);
                    addTo (k, b.step);
                }
            }
            else
            {
                auto& ost = b.oldSt[(size_t) c];
                for (int i = 0; i < n; ++i)
                {
                    const double w = smoothFade (1.0 - (double) (b.fadeLeft - i) / (double) fadeLength);
                    const double yNew = svfTick (k, st, x[i]);
                    const double yOld = svfTick (b.old, ost, x[i]);
                    x[i] = yOld + w * (yNew - yOld);
                    addTo (k, b.step);
                }
            }
        }
        b.cur = target;
        if (fading)
            b.fadeLeft = std::max (0, b.fadeLeft - n);
    }
}

//==============================================================================
// Compressor
// Constants fitted to Pro-C 3 Clean (Tools/proc3/fit_dynamics.py --two, mac-results/proc3-clean/fit_dynamics_two.json).
const Compressor::Model Compressor::model = {
    0.435428, 0.0177726, 10.6408, 1.53095, 3.43821,
    0.346, 6.5376, 1.0376, 0.631316, 1.17653,
    172.991, 16.0915, 1.02412,
    0.0982484, 11.595, 8.37701, 0.135851,
    719.945, 0.297099, 82.528
};

double Compressor::autoReleaseMs (double releaseMs) noexcept
{
    // Measured: with Auto Release, Pro-C 3 behaves as a manual release of 300 ms for knob settings of 100 ms and
    // more, and of about 54 ms at 10 ms.
    return 300.0 * std::pow (std::min (1.0, std::max (1.0, releaseMs) / 100.0), 0.745);
}

void Compressor::prepare (double sampleRate)
{
    fs = sampleRate;
    sbCoeff = std::exp (-(double) subBlock / (0.02 * fs));
    const auto coeff = [this] (double seconds) { return -std::expm1 (-(double) autoMakeupStep / (seconds * fs)); };
    autoCoeffSlow = coeff (3.0);
    autoCoeffFall = coeff (1.0);
    autoCoeffGlide = coeff (0.3);
    autoHoldSamples = (int) (2.0 * fs);
    autoMoveSamples = (int) (0.15 * fs);
    reset();
}

void Compressor::reset()
{
    first = true;
    fast = slow = slow2 = 0.0;
    hpfSt = {};
    minGainDb = 0.0f;
    autoIn = autoOut = autoDb = 0.0;
    autoSumE = autoSumEGr = autoDeepestGr = 0.0;
    autoCount = 0;
    autoHoldLeft = autoHoldSamples;
    autoMoveLeft = 0;
}

double Compressor::staticGainDb (double levelDb, double thresholdDb, double slope, double kneeDb) noexcept
{
    // Pro-C 3's measured curve: h(over, knee), interpolated between the measured knee widths. Knee 0 is exact.
    using namespace kneetable;
    const double over = levelDb - thresholdDb;
    if (kneeDb <= 0.0)
        return over > 0.0 ? -slope * over : 0.0;
    if (over <= overStart)
        return 0.0;
    const auto column = [over] (int k)
    {
        const double x = (over - overStart) / overStep;
        if (x >= numCols - 1)
            return h[k][numCols - 1] + (over - (overStart + overStep * (numCols - 1)));
        const int i = (int) x;
        const double f = x - i;
        return h[k][i] + f * (h[k][i + 1] - h[k][i]);
    };
    const double kw = std::min (kneeDb, kneetable::kneeDb[numKnees - 1]);
    int k = 0;
    while (k < numKnees - 2 && kneetable::kneeDb[k + 1] < kw)
        ++k;
    const double f = (kw - kneetable::kneeDb[k]) / (kneetable::kneeDb[k + 1] - kneetable::kneeDb[k]);
    return -slope * (column (k) + f * (column (k + 1) - column (k)));
}

double Compressor::staticGainDb (double levelDb) const noexcept
{
    return staticGainDb (levelDb, threshold, slope, kneeDb);
}

void Compressor::setTargets (const CompSettings& s, bool enabled)
{
    tThreshold = s.thresholdDb;
    tSlope = s.ratio >= 1000.0 ? 1.0 : 1.0 - 1.0 / std::max (1.0, s.ratio);
    tKneeDb = kneeWidthDb (s.knee);
    tManualMakeupDb = s.makeupDb;
    autoOn = s.autoMakeup;
    tMakeupDb = tManualMakeupDb + (autoOn ? autoDb : 0.0);
    tMix = std::clamp (s.mix, 0.0, 1.0);
    tOn = enabled ? 1.0 : 0.0;

    const auto& m = model;
    const double A = std::max (0.005, s.attackMs);
    const double R = s.autoRelease ? autoReleaseMs (s.releaseMs) : std::max (1.0, s.releaseMs);
    const double ms = 1e-3 * fs;
    attackBase = (m.kA * A + m.c0) * ms;
    t0Samples = m.t0 * ms;
    invD0 = 1.0 / m.d0;
    relFast = std::exp (-1.0 / (std::max (m.tMin, m.kF * R + m.cF - m.kD * std::pow (A, m.qD)) * ms));
    relSlow = std::exp (-1.0 / (std::max (m.tMin, m.kS * R + m.cS - m.kDs * std::pow (A, m.qDs)) * ms));
    charge = std::exp (-1.0 / (m.tc * (1.0 + std::pow (A / m.a0c, m.qc)) * ms));
    charge2 = std::exp (-1.0 / (m.tc2 * (1.0 + std::pow (A / m.a0c, m.qc)) * ms));
    relSlow2 = std::exp (-1.0 / (std::max (m.tMin, m.kS2 * R + m.cS2 - m.kDs * std::pow (A, m.qDs)) * ms));

    tHpfHz = s.sidechainHpfHz;

    // A move of anything that changes the reduction restarts AUTO makeup's measurement, once the move has ended.
    const std::array<double, 7> knobs { s.thresholdDb, s.ratio, s.knee, s.attackMs, s.releaseMs, s.autoRelease ? 1.0 : 0.0, s.sidechainHpfHz };
    if (knobs != autoKnobs)
    {
        autoKnobs = knobs;
        if (! first)
            autoMoveLeft = autoMoveSamples;
    }

    if (first)
    {
        threshold = tThreshold;
        slope = tSlope;
        kneeDb = tKneeDb;
        makeupDb = tMakeupDb;
        mix = tMix;
        on = tOn;
        first = false;
    }
}

// Gain reduction (dB, >= 0) for this sample's target: distance-dependent attack, fast release and the slow
// store, the deeper of the two.
double Compressor::smooth (double target) noexcept
{
    if (target > fast)
    {
        const double d = target - fast;
        const double tau = attackBase + t0Samples / (1.0 + std::pow (d * invD0, model.p));
        fast += d * -std::expm1 (-1.0 / tau);
    }
    else
    {
        fast = target + (fast - target) * relFast;
    }
    // The slow stores charge only while the signal holds the reduction, and in release follow the fast path down.
    if (target >= fast && fast > slow)
        slow = fast + (slow - fast) * charge;
    else if (target < fast && slow > fast)
        slow = fast + (slow - fast) * relSlow;
    if (target >= fast && fast > slow2)
        slow2 = fast + (slow2 - fast) * charge2;
    else if (target < fast && slow2 > fast)
        slow2 = fast + (slow2 - fast) * relSlow2;
    return std::max (fast, std::max (slow, slow2));
}

void Compressor::process (double* const* ch, int numChannels, int n) noexcept
{
    // Parameter ramps for this sub-block.
    const double thr0 = threshold, slope0 = slope, knee0 = kneeDb;
    threshold = approach (threshold, tThreshold, sbCoeff, 1e-6);
    slope = approach (slope, tSlope, sbCoeff, 1e-9);
    kneeDb = approach (kneeDb, tKneeDb, sbCoeff, 1e-6);
    tMakeupDb = tManualMakeupDb + (autoOn ? autoDb : 0.0);
    makeupRamp = rampTo (makeupDb, tMakeupDb, sbCoeff, 1e-6, n);
    mixRamp = rampTo (mix, tMix, sbCoeff, 1e-6, n);
    onRamp = rampTo (on, tOn, std::pow (sbCoeff, 3.0), 1e-6, n);   // IN/OUT faster than the knobs
    const double dThr = (threshold - thr0) / n, dSlope = (slope - slope0) / n, dKnee = (kneeDb - knee0) / n;

    // Sidechain high-pass.
    const bool hpfOn = tHpfHz > 0.0;
    if (hpfOn)
    {
        if (hpfHz <= 0.0)
        {
            hpfHz = tHpfHz;
            hpfSt = {};
        }
        hpfHz = std::exp (approach (std::log (hpfHz), std::log (tHpfHz), sbCoeff, 1e-6));
        const double g = std::tan (pi * std::min (hpfHz, 0.45 * fs) / fs);
        hpf = coeffsFromSvf ({ g, hpfK, 1.0, -hpfK, -1.0 });   // HP = x - k*BP - LP
    }
    else
    {
        hpfHz = 0.0;
    }

    const bool out = on == 0.0 && onRamp.step == 0.0;
    double thr = thr0, slp = slope0, knee = knee0;
    for (int i = 0; i < n; ++i)
    {
        double level = 0.0, energy = 0.0;
        for (int c = 0; c < numChannels; ++c)
        {
            double s = ch[c][i];
            energy += s * s;
            if (hpfOn)
                s = svfTick (hpf, hpfSt[(size_t) c], s);
            level = std::max (level, std::abs (s));
        }

        // Stereo-linked instantaneous peak -> static curve (dB) -> attack/release on the gain reduction (dB).
        // Out: the detector keeps tracking so switching in is seamless, but the audio is untouched.
        const double gr = -smooth (-staticGainDb (20.0 * std::log10 (level + 1e-30), thr, slp, knee));
        thr += dThr;
        slp += dSlope;
        knee += dKnee;

        // AUTO makeup reads the detector here, also while the compressor is out, so switching in is already settled.
        autoSumE += energy;
        autoSumEGr += energy * gr;
        autoDeepestGr = std::min (autoDeepestGr, gr);
        if (++autoCount == autoMakeupStep)
            stepAutoMakeup();

        if (out)
            continue;
        minGainDb = std::min (minGainDb, (float) gr);
        const double gain = dbToGain (gr + makeupRamp.next());
        const double m = mixRamp.next(), o = onRamp.next();
        for (int c = 0; c < numChannels; ++c)
        {
            const double x = ch[c][i];
            const double y = x + m * (x * gain - x);
            ch[c][i] = x + o * (y - x);
        }
    }
}

void Compressor::stepAutoMakeup() noexcept
{
    const double e = autoSumE / autoMakeupStep;
    const double eOut = autoSumE > 0.0 ? e * dbToGain (2.0 * autoSumEGr / autoSumE) : 0.0;
    const double deepest = -autoDeepestGr;
    autoSumE = autoSumEGr = autoDeepestGr = 0.0;
    autoCount = 0;

    // Mean power weights loud passages the most, as loudness does. So the makeup rises soon when the music gets
    // louder, and holds through quieter moments and gaps instead of riding them.
    autoIn += autoCoeffSlow * (e - autoIn);
    autoOut += autoCoeffSlow * (eOut - autoOut);

    // A knob has stopped moving: nearly empty the average, so the makeup doesn't wait for the old reduction to leave
    // it. New audio then counts almost alone, and the window grows back to its normal length.
    if (autoMoveLeft > 0)
    {
        autoMoveLeft -= autoMakeupStep;
        if (autoMoveLeft <= 0)
        {
            autoMoveLeft = 0;
            autoIn *= 1.0e-3;
            autoOut *= 1.0e-3;
        }
    }

    // The compressor has been doing much less than the makeup assumes (a quiet passage or silence): let it go, so a
    // signal below the threshold isn't left louder than it came in.
    if (deepest >= 0.5 * autoDb)
        autoHoldLeft = autoHoldSamples;
    else if (autoHoldLeft > 0)
        autoHoldLeft -= autoMakeupStep;
    else
        autoOut += autoCoeffFall * (autoIn - autoOut);

    if (autoIn < 1.0e-30 || autoOut < 1.0e-30)
        autoIn = autoOut = 0.0;
    const double target = autoIn > 0.0 ? autoMakeupShare * 10.0 * std::log10 (autoIn / autoOut) : 0.0;
    autoDb += autoCoeffGlide * (std::max (0.0, target) - autoDb);
}

//==============================================================================
// Engine
void Engine::prepare (double sampleRate, int maxBlockSize, int)
{
    fs = sampleRate;
    maxBlock = maxBlockSize;
    for (auto& s : slots)
    {
        s.eq.prepare (fs);
        s.eqSide.prepare (fs);
        s.comp.prepare (fs);
    }
    slots[0].compFirst = false;
    slots[1].compFirst = true;

    sbCoeff = std::exp (-(double) subBlock / (0.015 * fs));
    orderFadeLength = std::max (1, (int) std::lround (0.02 * fs));
    rmsCoeff = std::exp (-1.0 / (0.3 * fs));
    modeFadeSamples = std::max (1, (int) std::lround (0.005 * fs));

    reset();
}

void Engine::reset()
{
    for (auto& s : slots)
    {
        s.eq.reset();
        s.eqSide.reset();
        s.comp.reset();
    }
    active = 0;
    orderFadeLeft = 0;
    inMs = {};
    outMs = {};
    midMs = sideMs = 0.0;
    for (auto& d : dryDelay)
        d.fill (0.0);
    modeGain = 1.0;
    first = true;
}

void Engine::applyTargets (const Settings& s)
{
    for (auto& slot : slots)
    {
        slot.eq.setTargets (s.bands, s.eqOn);
        if (msPending || msActive)
            slot.eqSide.setTargets (s.sideBands, s.eqOn);
        slot.comp.setTargets (s.comp, s.dynOn);
    }
    if (first)
    {
        msActive = msPending;
        for (auto& slot : slots)
            slot.ms = msActive;
    }

    weissPending = s.weissEq;
    if (first && weissActive != weissPending)
    {
        weissActive = weissPending;
        for (auto& slot : slots)
        {
            slot.eq.setWeissMode (weissActive);
            slot.eqSide.setWeissMode (weissActive);
        }
    }

    const int desired = s.compFirst ? 1 : 0;
    if (first)
    {
        active = desired;
    }
    else if (desired != active && orderFadeLeft == 0)
    {
        orderFadeLeft = orderFadeLength;
    }

    tInGain = dbToGain (s.inputDb);
    tOutGain = s.outputDb <= outputOffDb ? 0.0 : dbToGain (s.outputDb);
    tEqOutGain = s.eqOn ? dbToGain (s.eqOutputDb) : 1.0;
    tMidG = dbToGain (s.midDb);
    tSideG = dbToGain (s.sideDb);
    const double b = std::clamp (s.balance, -1.0, 1.0);
    tBalL = b > 0.0 ? 1.0 - b : 1.0;
    tBalR = b < 0.0 ? 1.0 + b : 1.0;
    tMonoMix = s.mono ? 1.0 : 0.0;
    tBypassMix = s.bypass ? 1.0 : 0.0;

    if (first)
    {
        inGain = tInGain;
        outGain = tOutGain;
        eqOutGain = tEqOutGain;
        midG = tMidG;
        sideG = tSideG;
        balL = tBalL;
        balR = tBalR;
        monoMix = tMonoMix;
        bypassMix = tBypassMix;
        first = false;
    }
}

void Engine::process (float* const* io, int numChannels, int numSamples, const Settings& s, MeterData& m) noexcept
{
    const int nCh = std::min (numChannels, maxChannels);
    if (nCh <= 0 || numSamples <= 0)
        return;
    m.numChannels.store (nCh, std::memory_order_relaxed);

    msPending = s.msEq && nCh == 2;
    applyTargets (s);

    double work[maxChannels][subBlock];
    double* ch[maxChannels] = { work[0], work[1] };
    bool stateLost = false;

    for (int start = 0; start < numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, numSamples - start);
        // A sample that can't be audio stops here (DESIGN.md section 21): NaN, Inf, or beyond +120 dBFS (garbage or a runaway
        // filter upstream). Every filter, detector and meter below has feedback and keeps running with EQ or DYN out and
        // in bypass, so one such sample would stay in their state for good, or for minutes in AUTO makeup's averages.
        // Anything else passes untouched, so normal processing stays bit-identical. (NaN fails the comparison too.)
        for (int c = 0; c < nCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = io[c][start + i];
                work[c][i] = std::abs (x) <= maxInputSample ? (double) x : 0.0;
            }

        processSubBlock (ch, nCh, n, s, m);

        for (int c = 0; c < nCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                stateLost = stateLost || ! std::isfinite (work[c][i]);
                const float y = (float) work[c][i];
                io[c][start + i] = std::isfinite (y) ? y : 0.0f;   // never passed on, even when only the float overflowed
            }
    }

    // The input can't cause this any more. If anything else does (a host writing NaN to a parameter), start clean rather
    // than stay broken. reset() allocates nothing.
    if (stateLost)
    {
        reset();
        for (int c = 0; c < nCh; ++c)
            std::fill (io[c], io[c] + numSamples, 0.0f);
    }

    for (int c = 0; c < nCh; ++c)
    {
        m.inRms[(size_t) c].store ((float) std::sqrt (inMs[(size_t) c]), std::memory_order_relaxed);
        m.outRms[(size_t) c].store ((float) std::sqrt (outMs[(size_t) c]), std::memory_order_relaxed);
    }
    m.midRms.store ((float) std::sqrt (midMs), std::memory_order_relaxed);
    m.sideRms.store ((float) std::sqrt (sideMs), std::memory_order_relaxed);
}

void Engine::Slot::processEq (double* const* ch, int nCh, int n) noexcept
{
    if (! ms || nCh != 2)
    {
        eq.process (ch, nCh, n);
        return;
    }
    // M/S: encode, run Mid and Side each as one channel, decode. The Equalisers themselves are the same as in stereo.
    auto* a = ch[0];
    auto* b = ch[1];
    for (int i = 0; i < n; ++i)
    {
        const double l = a[i], r = b[i];
        a[i] = 0.5 * (l + r);
        b[i] = 0.5 * (l - r);
    }
    double* mid[1] = { a };
    double* side[1] = { b };
    eq.process (mid, 1, n);
    eqSide.process (side, 1, n);
    for (int i = 0; i < n; ++i)
    {
        const double m = a[i], s = b[i];
        a[i] = m + s;
        b[i] = m - s;
    }
}

void Engine::processSubBlock (double* const* ch, int nCh, int n, const Settings& settings, MeterData& m) noexcept
{
    // Weiss mode switch: applied once the output has faded to silence.
    if (weissPending != weissActive && modeGain <= 0.0)
    {
        weissActive = weissPending;
        for (auto& slot : slots)
        {
            slot.eq.setWeissMode (weissActive);
            slot.eqSide.setWeissMode (weissActive);
        }
        for (auto& d : dryDelay)
            d.fill (0.0);
    }
    // Stereo <-> M/S, the same way: the filters start again on their new signals while the output is silent.
    if (msPending != msActive && modeGain <= 0.0)
    {
        msActive = msPending;
        for (auto& slot : slots)
        {
            slot.ms = msActive;
            slot.eq.reset();
            slot.eqSide.reset();
            slot.eq.setTargets (settings.bands, settings.eqOn);
            slot.eqSide.setTargets (settings.sideBands, settings.eqOn);
        }
    }

    double dryBuf[maxChannels][subBlock];
    for (int c = 0; c < nCh; ++c)
        std::copy (ch[c], ch[c] + n, dryBuf[c]);
    if (weissActive)
    {
        // The bypass path has the EQ's latency too, so bypass stays in time with the processed signal.
        constexpr int L = weissLatencySamples;
        for (int c = 0; c < nCh; ++c)
        {
            auto& d = dryDelay[(size_t) c];
            std::copy (dryBuf[c], dryBuf[c] + n, d.begin() + L);
            std::copy (d.begin(), d.begin() + n, dryBuf[c]);
            std::copy (d.begin() + n, d.begin() + n + L, d.begin());
        }
    }

    // 1. Input trim and input meters.
    {
        const auto r = rampTo (inGain, tInGain, sbCoeff, 1e-9, n);
        for (int c = 0; c < nCh; ++c)
        {
            double g = r.value, peak = 0.0, ms = inMs[(size_t) c];
            auto* x = ch[c];
            for (int i = 0; i < n; ++i, g += r.step)
            {
                x[i] *= g;
                peak = std::max (peak, std::abs (x[i]));
                ms = x[i] * x[i] + rmsCoeff * (ms - x[i] * x[i]);
            }
            inMs[(size_t) c] = ms;
            MeterData::storeMax (m.inPeak[(size_t) c], (float) peak);
        }
    }

    // 2. EQ and dynamics. Both orders run all the time (the cost is small), so either is always warm and a
    //    change of order is a plain crossfade between two correctly settled chains.
    {
        double altBuf[maxChannels][subBlock];
        double* alt[maxChannels] = { altBuf[0], altBuf[1] };
        for (int c = 0; c < nCh; ++c)
            std::copy (ch[c], ch[c] + n, altBuf[c]);
        slots[0].process (ch, nCh, n);    // EQ -> DYN
        slots[1].process (alt, nCh, n);   // DYN -> EQ

        const float gr0 = slots[0].comp.takeMinGainDb(), gr1 = slots[1].comp.takeMinGainDb();
        const int shown = orderFadeLeft > 0 ? 1 - active : active;
        MeterData::storeMin (m.gainReductionDb, shown == 0 ? gr0 : gr1);
        MeterData::storeMin (m.hostGainReductionDb, shown == 0 ? gr0 : gr1);

        if (orderFadeLeft > 0)
        {
            // Crossfade from the active order to the other one.
            for (int c = 0; c < nCh; ++c)
                for (int i = 0; i < n; ++i)
                {
                    double w = smoothFade (1.0 - (double) (orderFadeLeft - i) / (double) orderFadeLength);
                    if (active == 1)
                        w = 1.0 - w;   // fading from DYN -> EQ (alt) to EQ -> DYN (ch)
                    ch[c][i] += w * (alt[c][i] - ch[c][i]);
                }
            orderFadeLeft = std::max (0, orderFadeLeft - n);
            if (orderFadeLeft == 0)
                active = 1 - active;
        }
        else if (active == 1)
        {
            for (int c = 0; c < nCh; ++c)
                std::copy (alt[c], alt[c] + n, ch[c]);
        }
    }

    // 3. Architecture: independent Mid and Side level, then balance.
    if (nCh == 2)
    {
        const bool idle = midG == 1.0 && tMidG == 1.0 && sideG == 1.0 && tSideG == 1.0
                          && balL == 1.0 && tBalL == 1.0 && balR == 1.0 && tBalR == 1.0;
        if (! idle)
        {
            auto rm = rampTo (midG, tMidG, sbCoeff, 1e-9, n);
            auto rs = rampTo (sideG, tSideG, sbCoeff, 1e-9, n);
            auto rl = rampTo (balL, tBalL, sbCoeff, 1e-9, n);
            auto rr = rampTo (balR, tBalR, sbCoeff, 1e-9, n);
            auto* L = ch[0];
            auto* R = ch[1];
            for (int i = 0; i < n; ++i)
            {
                const double mid = 0.5 * (L[i] + R[i]) * rm.next();
                const double side = 0.5 * (L[i] - R[i]) * rs.next();
                L[i] = (mid + side) * rl.next();
                R[i] = (mid - side) * rr.next();
            }
        }
    }
    else if (nCh == 1 && ! (midG == 1.0 && tMidG == 1.0))
    {
        // Mono: the signal is all Mid (Side is zero), so MID acts as a level control. SIDE and BALANCE have
        // nothing to act on. (v0.2.1 skipped the stage on mono, so MID did nothing on a mono track.)
        auto rm = rampTo (midG, tMidG, sbCoeff, 1e-9, n);
        for (int i = 0; i < n; ++i)
            ch[0][i] *= rm.next();
    }

    // 4. EQ Output and the output fader, after the compressor in both orders, so neither changes what it hears.
    //    Output and Mid/Side meters.
    {
        auto re = rampTo (eqOutGain, tEqOutGain, sbCoeff, 1e-9, n);
        auto ro = rampTo (outGain, tOutGain, sbCoeff, 1e-9, n);
        for (int i = 0; i < n; ++i)
        {
            const double g = re.next() * ro.next();
            for (int c = 0; c < nCh; ++c)
                ch[c][i] *= g;
        }
        for (int c = 0; c < nCh; ++c)
        {
            double peak = 0.0, ms = outMs[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                const double x = ch[c][i];
                peak = std::max (peak, std::abs (x));
                ms = x * x + rmsCoeff * (ms - x * x);
            }
            outMs[(size_t) c] = ms;
            MeterData::storeMax (m.outPeak[(size_t) c], (float) peak);
        }
        if (nCh == 2)
        {
            for (int i = 0; i < n; ++i)
            {
                const double mid = 0.5 * (ch[0][i] + ch[1][i]), side = 0.5 * (ch[0][i] - ch[1][i]);
                midMs = mid * mid + rmsCoeff * (midMs - mid * mid);
                sideMs = side * side + rmsCoeff * (sideMs - side * side);
            }
        }
        else
        {
            midMs = outMs[0];
            sideMs = 0.0;
        }
    }

    // 5. Mono: monitoring only, after every meter and every processing stage.
    if (nCh == 2 && ! (monoMix == 0.0 && tMonoMix == 0.0))
    {
        auto r = rampTo (monoMix, tMonoMix, std::pow (sbCoeff, 2.0), 1e-6, n);
        for (int i = 0; i < n; ++i)
        {
            const double w = r.next();
            const double sum = 0.5 * (ch[0][i] + ch[1][i]);
            ch[0][i] += w * (sum - ch[0][i]);
            ch[1][i] += w * (sum - ch[1][i]);
        }
    }

    // 6. Bypass: crossfade back to the untouched input.
    if (! (bypassMix == 0.0 && tBypassMix == 0.0))
    {
        auto r = rampTo (bypassMix, tBypassMix, std::pow (sbCoeff, 2.0), 1e-6, n);
        for (int i = 0; i < n; ++i)
        {
            const double w = r.next();
            for (int c = 0; c < nCh; ++c)
                ch[c][i] += w * (dryBuf[c][i] - ch[c][i]);
        }
    }

    // 7. EQ mode change (Weiss on/off, Stereo/M-S): 5 ms fade out, switch (top of the next sub-block), 5 ms fade in.
    const double modeTarget = (weissPending != weissActive || msPending != msActive) ? 0.0 : 1.0;
    if (! (modeGain == 1.0 && modeTarget == 1.0))
    {
        const double delta = (double) n / (double) modeFadeSamples;
        const double start = modeGain;
        modeGain = modeTarget > start ? std::min (modeTarget, start + delta) : std::max (modeTarget, start - delta);
        const double step = (modeGain - start) / (double) n;
        for (int c = 0; c < nCh; ++c)
        {
            double g = start;
            for (int i = 0; i < n; ++i, g += step)
                ch[c][i] *= g;
        }
    }
}
} // namespace coherence::dsp
