// COHERENCE - headless measurement and test runner.
//
//   CoherenceTests                       run every check, print a report, exit non-zero on failure
//   CoherenceTests --report out.md       ... and write the measurement report as Markdown
//   CoherenceTests --snapshot out.png    render the editor (2x) after feeding it some audio
//   CoherenceTests --approved-snapshot out.png [theme [window scale [side]]]   the panel at the approved design's control values
//   CoherenceTests --render-q dir [fs]   render the MDWEQ6 comparison settings as impulse responses (default 48 kHz)
//   CoherenceTests --render-ref dir      render the reference-kit settings (REFERENCES/EQ renders v2) as impulse responses
//   CoherenceTests --render-file in.wav out.wav thr=-20 ratio=4 att=10 rel=150 knee=0 ...
//                                        process a 32-bit float WAV through the compressor (EQ out)
//
// The checks measure the actual processing (impulse responses and test signals through the engine), not just
// the coefficient design.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UserSettings.h"
#include "dsp/Engine.h"
#include "dsp/EqDesign.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>

using namespace coherence;
using namespace coherence::dsp;

namespace
{
int failures = 0;
juce::String report;

void line (const juce::String& s)
{
    std::printf ("%s\n", s.toRawUTF8());
    report << s << "\n";
}

void check (bool ok, const juce::String& what)
{
    line (juce::String (ok ? "  PASS  " : "  FAIL  ") + what);
    if (! ok)
        ++failures;
}

// A measured limitation that is understood and documented (DESIGN.md), reported as such rather than as a pass.
int limits = 0;
void limitation (const juce::String& what)
{
    line ("  LIMIT " + what);
    ++limits;
}

juce::String fmt (double v, int d = 3) { return juce::String (v, d); }

// The measurements set parameters the way a host does. They run with gain stepping at Precision (0.01 dB), where
// what is set is what plays; the stepping modes have their own tests ("Gain stepping from every control path").
// The file is selected here; it is written once at start-up (main), while no settings object exists.
void useDefaultTestSettings()
{
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("coherence-default-settings").getChildFile ("COHERENCE.settings");
    f.getParentDirectory().createDirectory();
    UserSettings::setFileForTesting (f);
}

// Run a stereo float signal through an engine with fixed settings.
std::vector<std::vector<float>> run (Engine& e, const Settings& s, const std::vector<std::vector<float>>& in, int block = 256)
{
    MeterData m;
    auto out = in;
    const int n = (int) in[0].size();
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        float* ptrs[2] = { out[0].data() + start, out[1].data() + start };
        e.process (ptrs, 2, len, s, m);
    }
    return out;
}

Settings neutral()
{
    Settings s;
    s.bands[0] = { Shape::lowShelf, 80.0, 0.0, 0.71 };
    s.bands[1] = { Shape::bell, 320.0, 0.0, 1.0 };
    s.bands[2] = { Shape::bell, 2500.0, 0.0, 1.0 };
    s.bands[3] = { Shape::highShelf, 10000.0, 0.0, 0.71 };
    return s;
}

std::vector<std::vector<float>> noise (int n, float amp, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> d (0.0f, amp);
    std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n));
    for (auto& c : x)
        for (auto& v : c)
            v = d (rng);
    return x;
}

std::vector<std::vector<float>> sine (int n, double fs, double f, float amp, float ampR = -1.0f)
{
    std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n));
    for (int i = 0; i < n; ++i)
    {
        const float v = (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / fs);
        x[0][(size_t) i] = amp * v;
        x[1][(size_t) i] = (ampR < 0 ? amp : ampR) * v;
    }
    return x;
}

// Square wave: |x| is constant, so a peak detector reads its level exactly.
std::vector<std::vector<float>> squareWave (int n, double fs, double f, float amp)
{
    std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n));
    for (int i = 0; i < n; ++i)
        x[0][(size_t) i] = x[1][(size_t) i] = std::fmod (f * i / fs, 1.0) < 0.5 ? amp : -amp;
    return x;
}

double rmsDb (const std::vector<float>& x, size_t from = 0)
{
    double s = 0;
    for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / (double) (x.size() - from) + 1e-30);
}

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
{
    double m = 0;
    for (size_t i = from; i < a.size(); ++i) m = std::max (m, (double) std::abs (a[i] - b[i]));
    return m;
}

// Magnitude (dB) of an impulse response at frequency f.
double irMagnitudeDb (const std::vector<double>& h, double f, double fs)
{
    std::complex<double> acc = 0.0;
    const double w = -2.0 * juce::MathConstants<double>::pi * f / fs;
    for (size_t n = 0; n < h.size(); ++n)
        acc += h[n] * std::polar (1.0, w * (double) n);
    return 20.0 * std::log10 (std::abs (acc) + 1e-300);
}

std::vector<double> logFreqs (double lo, double hi, int n)
{
    std::vector<double> f ((size_t) n);
    for (int i = 0; i < n; ++i) f[(size_t) i] = lo * std::pow (hi / lo, (double) i / (n - 1));
    return f;
}

double designError (Shape shape, double f0, double g, double q, double fs, int which)   // 0 plug-in, 1 matched, 2 bilinear
{
    Biquad b = which == 0 ? biquadFromSvf (designBand (shape, f0, g, q, fs))
                          : (which == 1 ? designMatched (shape, f0, g, q, fs) : designBilinear (shape, f0, g, q, fs));
    double e = 0;
    for (double f : logFreqs (20.0, std::min (20000.0, 0.4535 * fs), 240))
    {
        const double ref = analogMagnitudeDb (shape, f0, g, q, f);
        if ((shape == Shape::lowCut || shape == Shape::highCut) && ref < -24.0)
            continue;   // stopband of the cut filters: irrelevant in dB terms
        e = std::max (e, std::abs (biquadMagnitudeDb (b, f, fs) - ref));
    }
    return e;
}

// The design the engine runs for a band set on the panel (bells: panel Q -> design Q).
Biquad engineDesign (Shape shape, double f0, double g, double q, double fs)
{
    double dq = effectiveQ (shape, q);
    if (shape == Shape::bell)
        dq = bellDesignQ (dq, g);
    return biquadFromSvf (designBand (shape, f0, g, dq, fs));
}

//==============================================================================
void eqAccuracy()
{
    line ("\n## EQ: magnitude accuracy against the analog prototype\n");
    line ("Worst-case |error| in dB over 20 Hz - 20 kHz for gains of +/-0.5..15 dB.");
    line ("Bells: Q 0.3 - 10. Shelves: Q 0.5 - 1.0 (high) / 0.71 (low). Cuts: Q 0.5 - 2, passband and transition only.\n");
    line ("| Band | f0 | 44.1 kHz: COHERENCE / bilinear | 48 kHz | 96 kHz |");
    line ("|---|---|---|---|---|");

    struct Case { Shape shape; const char* name; std::vector<double> f0s; std::vector<double> qs; std::vector<double> gains; };
    const std::vector<Case> cases {
        { Shape::bell, "Bell", { 100, 1000, 4000, 8000, 12000, 16000, 18000 }, { 0.3, 0.7, 1.5, 4, 10 }, { -15, -6, -2, -0.5, 0.5, 2, 6, 15 } },
        { Shape::highShelf, "High shelf", { 2000, 8000, 12000, 16000 }, { 0.5, 0.71, 1.0 }, { -15, -6, -2, 2, 6, 15 } },
        { Shape::lowShelf, "Low shelf", { 30, 100, 300, 1000 }, { 0.71 }, { -15, -6, 6, 15 } },
        { Shape::highCut, "High cut", { 2000, 8000, 16000 }, { 0.5, 0.71, 1.0, 2.0 }, { 0 } },
        { Shape::lowCut, "Low cut", { 20, 60, 200 }, { 0.5, 0.71, 1.0, 2.0 }, { 0 } },
    };
    double worstBellFit16k = 0;
    for (const auto& c : cases)
        for (double f0 : c.f0s)
        {
            juce::String row = juce::String ("| ") + c.name + " | " + (f0 >= 1000 ? juce::String (f0 / 1000.0, 1) + "k" : juce::String ((int) f0)) + " |";
            for (double fs : { 44100.0, 48000.0, 96000.0 })
            {
                double e0 = 0, e2 = 0;
                for (double q : c.qs)
                    for (double g : c.gains)
                    {
                        e0 = std::max (e0, designError (c.shape, f0, g, q, fs, 0));
                        e2 = std::max (e2, designError (c.shape, f0, g, q, fs, 2));
                    }
                if (c.shape == Shape::bell && f0 <= 16000)
                    worstBellFit16k = std::max (worstBellFit16k, e0);
                row << " " << fmt (e0, 2) << " / " << fmt (e2, 2) << " |";
            }
            line (row);
        }
    line ("");
    check (worstBellFit16k < 1.0, "bells up to 16 kHz within 1 dB of analog at every setting (worst " + fmt (worstBellFit16k, 2) + " dB)");

    // Typical mastering moves.
    double typ = 0;
    for (double fs : { 44100.0, 48000.0 })
        for (double f0 : { 60.0, 250.0, 1000.0, 3000.0, 6000.0, 10000.0, 14000.0 })
            for (double q : { 0.5, 0.7, 1.0, 2.0 })
                for (double g : { -3.0, -1.0, -0.5, 0.5, 1.0, 3.0 })
                    typ = std::max (typ, designError (Shape::bell, f0, g, q, fs, 0));
    check (typ < 0.15, "typical bell moves (+/-3 dB, Q 0.5-2, 60 Hz-14 kHz, 44.1/48k) within 0.15 dB of analog (worst " + fmt (typ, 3) + " dB)");
}

void eqImplementation()
{
    line ("\n## EQ: the running filter matches its design\n");
    // Impulse response through the full engine with a single band active.
    double worst = 0;
    for (double fs : { 44100.0, 96000.0 })
    {
        for (auto [shape, f0, g, q] : { std::tuple { Shape::bell, 40.0, 6.0, 4.0 }, std::tuple { Shape::bell, 15000.0, -9.0, 0.7 },
                                        std::tuple { Shape::highShelf, 12000.0, 4.0, 0.71 }, std::tuple { Shape::lowCut, 30.0, 0.0, 0.71 } })
        {
            Engine e;
            e.prepare (fs, 512, 2);
            auto s = neutral();
            const int band = (shape == Shape::lowCut || shape == Shape::lowShelf) ? 0 : (shape == Shape::bell ? (f0 < 1000 ? 1 : 2) : 3);
            s.bands[(size_t) band] = { shape, f0, g, q };
            const int n = (int) (fs * 2.0);
            std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n, 0.0f));
            x[0][0] = x[1][0] = 1.0f;
            auto y = run (e, s, x);
            std::vector<double> h (y[0].begin(), y[0].end());
            const auto design = engineDesign (shape, f0, g, q, fs);
            for (double f : logFreqs (20.0, 20000.0, 60))
                worst = std::max (worst, std::abs (irMagnitudeDb (h, f, fs) - biquadMagnitudeDb (design, f, fs)));
        }
    }
    check (worst < 0.01, "measured impulse response equals the designed response (max deviation " + fmt (worst, 5) + " dB)");

    // Boost and cut cancel.
    {
        Engine a, b;
        a.prepare (48000, 512, 2);
        b.prepare (48000, 512, 2);
        auto s1 = neutral(), s2 = neutral();
        s1.bands[2] = { Shape::bell, 3500.0, 7.5, 2.3 };
        s2.bands[2] = { Shape::bell, 3500.0, -7.5, 2.3 };
        s1.bands[3] = { Shape::highShelf, 9000.0, 4.0, 0.8 };
        s2.bands[3] = { Shape::highShelf, 9000.0, -4.0, 0.8 };
        auto x = noise (48000, 0.1f);
        auto y = run (b, s2, run (a, s1, x));
        const double resid = rmsDb (std::vector<float> ([&] { std::vector<float> d (x[0].size()); for (size_t i = 0; i < d.size(); ++i) d[i] = y[0][i] - x[0][i]; return d; }()), 1000)
                             - rmsDb (x[0], 1000);
        check (resid < -100.0, "boost followed by the same cut nulls to " + fmt (resid, 1) + " dB (reciprocal design)");
    }
}

void eqStructures()
{
    line ("\n## EQ: filter structure under modulation and at low frequencies\n");
    // 1. Modulation: sweep a +12 dB, Q 2 bell from 100 Hz to 12 kHz in 150 ms over a 1 kHz sine, per-sample coefficient
    //    updates in both structures; compare the output against a reference computed at 4x the update rate.
    const double fs = 48000.0;
    const int n = (int) (fs * 0.3);
    auto runStructure = [&] (bool svf, int updateEvery)
    {
        std::vector<double> y ((size_t) n);
        SvfState st;
        double z1 = 0, z2 = 0;   // TDF2
        for (int i = 0; i < n; ++i)
        {
            const double t = std::min (1.0, (double) i / (0.15 * fs));
            const int k = (i / updateEvery) * updateEvery;
            const double tk = std::min (1.0, (double) k / (0.15 * fs));
            (void) t;
            const double f0 = 100.0 * std::pow (120.0, tk);
            const double x = 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / fs);
            const auto p = designBand (Shape::bell, f0, 12.0, 2.0, fs);
            if (svf)
                y[(size_t) i] = svfTick (coeffsFromSvf (p), st, x);
            else
            {
                const auto b = biquadFromSvf (p);
                const double out = b.b0 * x + z1;
                z1 = b.b1 * x - b.a1 * out + z2;
                z2 = b.b2 * x - b.a2 * out;
                y[(size_t) i] = out;
            }
        }
        return y;
    };
    auto hfEnergy = [&] (const std::vector<double>& y)
    {
        // Energy above 16 kHz (the 1 kHz input and its modulation sidebands live well below).
        double e = 0;
        for (double f = 16000.0; f < 23500.0; f += 250.0)
        {
            std::complex<double> acc = 0;
            for (size_t i = 0; i < y.size(); ++i)
                acc += y[i] * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f * (double) i / fs);
            e += std::norm (acc);
        }
        double tot = 0;
        for (double v : y) tot += v * v;
        return 10.0 * std::log10 (e / (tot * (double) y.size()) + 1e-300);
    };
    const double svfStepped = hfEnergy (runStructure (true, subBlock));
    const double tdfStepped = hfEnergy (runStructure (false, subBlock));
    line ("Bell +12 dB Q 2 swept 100 Hz -> 12 kHz in 150 ms, coefficients changed every " + juce::String (subBlock)
          + " samples (no interpolation), 1 kHz sine: energy above 16 kHz relative to the signal");
    line ("- state-variable (COHERENCE): " + fmt (svfStepped, 1) + " dB");
    line ("- transposed direct form II biquad: " + fmt (tdfStepped, 1) + " dB");
    check (svfStepped <= tdfStepped + 0.5, "SVF modulation artifacts no worse than a direct-form biquad");

    // 2. Low-frequency precision: 25 Hz bell at 192 kHz, float vs double.
    {
        const double fs2 = 192000.0;
        const auto p = designBand (Shape::bell, 25.0, 6.0, 3.0, fs2);
        const auto c = coeffsFromSvf (p);
        const auto b = biquadFromSvf (p);
        std::mt19937 rng (7);
        std::normal_distribution<double> d (0.0, 0.1);
        SvfState sd;
        float z1f = 0, z2f = 0;
        long double z1l = 0, z2l = 0;
        double errSvf = 0, errTdfFloat = 0, sig = 0;
        const int len = (int) fs2 * 2;
        for (int i = 0; i < len; ++i)
        {
            const double x = d (rng);
            const long double ol = (long double) b.b0 * x + z1l;   // long double reference
            z1l = (long double) b.b1 * x - (long double) b.a1 * ol + z2l;
            z2l = (long double) b.b2 * x - (long double) b.a2 * ol;
            const double ys = svfTick (c, sd, x);
            const float of = (float) b.b0 * (float) x + z1f;
            z1f = (float) b.b1 * (float) x - (float) b.a1 * of + z2f;
            z2f = (float) b.b2 * (float) x - (float) b.a2 * of;
            if (i > len / 4)
            {
                errSvf += (double) ((ys - ol) * (ys - ol));
                errTdfFloat += (double) ((of - ol) * (of - ol));
                sig += (double) (ol * ol);
            }
        }
        const double nSvf = 10.0 * std::log10 (errSvf / sig + 1e-300), nTdf = 10.0 * std::log10 (errTdfFloat / sig + 1e-300);
        line ("\nRound-off noise, 25 Hz bell +6 dB Q 3 at 192 kHz, relative to the output:");
        line ("- double-precision SVF (COHERENCE): " + fmt (nSvf, 1) + " dB");
        line ("- single-precision direct-form biquad (typical plug-in): " + fmt (nTdf, 1) + " dB");
        check (nSvf < -200.0, "low-frequency round-off noise below -200 dB");
    }
}

//==============================================================================
// EQ technical review (v0.2)

struct UiShape { Shape shape; const char* name; double fLo, fHi, qLo, qHi; bool hasGain; };
const UiShape uiShapes[] = {
    { Shape::bell, "Bell (all bands)", 16.0, 20000.0, 0.3, 10.0, true },
    { Shape::highShelf, "High shelf (HF)", 1000.0, 20000.0, 0.35, 2.0, true },
    { Shape::lowShelf, "Low shelf (LF)", 16.0, 1000.0, 0.35, 2.0, true },
    { Shape::highCut, "High cut (HF)", 1000.0, 20000.0, 0.3, 4.0, false },
    { Shape::lowCut, "Low cut (LF)", 16.0, 1000.0, 0.3, 4.0, false },
};

double poleRadius (const Biquad& b)
{
    // |roots of z^2 + a1 z + a2|
    const double disc = b.a1 * b.a1 - 4.0 * b.a2;
    if (disc < 0.0) return std::sqrt (b.a2);
    const double r1 = std::abs ((-b.a1 + std::sqrt (disc)) * 0.5), r2 = std::abs ((-b.a1 - std::sqrt (disc)) * 0.5);
    return std::max (r1, r2);
}

void eqFullRange()
{
    line ("\n## EQ review: the whole operating range\n");
    line ("4,000 random settings per band type and sample rate, drawn from the panel's full ranges (gain +/-15 dB),");
    line ("compared with the analog prototype from 20 Hz to 20 kHz (or 0.4535 fs). Also the largest pole radius of any");
    line ("design (stability needs < 1).\n");
    line ("| Band type | Sample rate | Max error | 99th percentile | Median | Max pole radius | Worst case |");
    line ("|---|---|---|---|---|---|---|");
    std::mt19937 rng (42);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    double worstPole = 0;
    bool allStable = true;
    for (const auto& s : uiShapes)
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            std::vector<double> errs;
            double maxPole = 0, worstE = 0;
            juce::String worstAt;
            for (int i = 0; i < 4000; ++i)
            {
                const double f0 = s.fLo * std::pow (s.fHi / s.fLo, u (rng));
                const double q = s.qLo * std::pow (s.qHi / s.qLo, u (rng));
                const double g = s.hasGain ? -15.0 + 30.0 * u (rng) : 0.0;
                errs.push_back (designError (s.shape, f0, g, q, fs, 0));
                if (errs.back() > worstE) { worstE = errs.back(); worstAt = fmt (f0, 0) + " Hz, Q " + fmt (q, 2) + ", " + fmt (g, 1) + " dB"; }
                const auto b = biquadFromSvf (designBand (s.shape, f0, g, effectiveQ (s.shape, q), fs));
                maxPole = std::max (maxPole, poleRadius (b));
                allStable = allStable && std::isfinite (b.b0) && poleRadius (b) < 1.0;
            }
            std::sort (errs.begin(), errs.end());
            worstPole = std::max (worstPole, maxPole);
            line (juce::String ("| ") + s.name + " | " + fmt (fs / 1000.0, 1) + " kHz | " + fmt (errs.back(), 3) + " dB | "
                  + fmt (errs[(size_t) (errs.size() * 0.99)], 3) + " dB | " + fmt (errs[errs.size() / 2], 4) + " dB | " + fmt (maxPole, 9) + " | " + worstAt + " |");
        }
    check (allStable, "every design across the full range is stable (largest pole radius " + fmt (worstPole, 9) + ")");

    // Boosts must be minimum phase, or the exact-inverse cut would not exist.
    int fallbacks = 0, total = 0;
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int i = 0; i < 3000; ++i)
        {
            const double f0 = 16.0 * std::pow (20000.0 / 16.0, u (rng)), q = 0.3 * std::pow (10.0 / 0.3, u (rng)), g = 15.0 * u (rng) + 0.01;
            for (Shape sh : { Shape::bell, Shape::highShelf })
            {
                const auto b = biquadFromSvf (designBand (sh, f0, g, effectiveQ (sh, q), fs));
                const Biquad inv { 1.0 / b.b0, b.a1 / b.b0, b.a2 / b.b0, b.b1 / b.b0, b.b2 / b.b0 };
                ++total;
                if (! (poleRadius (inv) < 1.0)) ++fallbacks;
            }
        }
    check (fallbacks == 0, "every boost is minimum phase, so every cut is the exact inverse (" + juce::String (fallbacks) + " exceptions in " + juce::String (total) + ")");
}

// Magnitude of the running filter, measured from its impulse response.
struct MeasuredIR
{
    std::vector<double> h;
    double fs;
    double db (double f) const { return irMagnitudeDb (h, f, fs); }
};

MeasuredIR measureBand (Shape shape, double f0, double g, double q, double fs)
{
    Engine e;
    e.prepare (fs, 512, 2);
    auto s = neutral();
    s.bands[2] = { shape, f0, g, q };
    const int n = (int) (fs * std::max (0.25, 12.0 * q / f0 + 0.05));
    std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n, 0.0f));
    x[0][0] = x[1][0] = 1.0f;
    auto y = run (e, s, x);
    return { std::vector<double> (y[0].begin(), y[0].end()), fs };
}

// Frequency (between lo and hi) where |H| crosses `level` dB, by bisection on a log scale.
double crossing (const MeasuredIR& m, double lo, double hi, double level, bool rising)
{
    for (int i = 0; i < 40; ++i)
    {
        const double mid = std::sqrt (lo * hi);
        const bool above = m.db (mid) > level;
        if (above == rising) hi = mid; else lo = mid;
    }
    return std::sqrt (lo * hi);
}

void eqMeasured()
{
    line ("\n## EQ review: frequency, gain and Q measured from the running filter\n");
    line ("Impulse responses through the engine (one bell active). Measured: the gain at the set frequency, the frequency");
    line ("of the extremum (golden-section search), and the bandwidth between the half-gain (dB) points.");
    line ("Q follows the classic / MDWEQ6 definition, for which the half-gain bandwidth corresponds to Q / 10^(|gain|/40);");
    line ("the Q error compares the measured half-gain bandwidth with that.\n");
    line ("| Sample rate | Range of f0 | Worst gain error at f0 | Worst Q error, Q 1-5 | Worst Q error, Q 0.5 | Extremum shift, Q 1-5 |");
    line ("|---|---|---|---|---|---|");
    double worstF = 0, worstG = 0, worstQ = 0;
    for (double fs : { 44100.0, 96000.0 })
        for (auto [lo, hi] : { std::pair { 30.0, 3000.0 }, std::pair { 3000.0, 12000.0 }, std::pair { 12000.0, 18000.0 } })
        {
            double ef = 0, eg = 0, eq = 0, eqWide = 0;
            for (double f0 : { lo, std::sqrt (lo * hi), hi })
                for (double q : { 0.5, 1.0, 2.0, 5.0 })
                    for (double g : { 12.0, 3.0, -6.0 })
                    {
                        const auto m = measureBand (Shape::bell, f0, g, q, fs);
                        // peak frequency
                        double a = f0 / 1.5, b = std::min (f0 * 1.5, 0.49 * fs);
                        const double sgn = g > 0 ? 1.0 : -1.0;
                        for (int i = 0; i < 60; ++i)
                        {
                            const double c1 = std::exp (std::log (a) + 0.382 * (std::log (b) - std::log (a)));
                            const double c2 = std::exp (std::log (a) + 0.618 * (std::log (b) - std::log (a)));
                            if (sgn * m.db (c1) > sgn * m.db (c2)) b = c2; else a = c1;
                        }
                        const double fp = std::sqrt (a * b), gp = m.db (fp);
                        if (q >= 1.0)
                            ef = std::max (ef, std::abs (fp / f0 - 1.0) * 100.0);
                        eg = std::max (eg, std::abs (m.db (f0) - g));
                        // half-gain bandwidth
                        const double half = gp * 0.5;
                        const double fl = crossing (m, std::max (5.0, fp / 64.0), fp, half, g > 0);
                        const double top = std::min (fp * 64.0, 0.4999 * fs);
                        if (sgn * m.db (top) < sgn * half)   // upper crossing exists below Nyquist
                        {
                            const double fh = crossing (m, fp, top, half, g < 0);
                            const double n = std::log2 (fh / fl);
                            const double qm = std::sqrt (std::pow (2.0, n)) / (std::pow (2.0, n) - 1.0);
                            const double e = std::abs (qm / bellDesignQ (q, g) - 1.0) * 100.0;
                            if (q >= 1.0) eq = std::max (eq, e); else eqWide = std::max (eqWide, e);
                        }
                    }
            worstF = std::max (worstF, ef); worstG = std::max (worstG, eg); worstQ = std::max (worstQ, eq);
            line ("| " + fmt (fs / 1000.0, 1) + " kHz | " + fmt (lo, 0) + " - " + fmt (hi, 0) + " Hz | " + fmt (eg, 4) + " dB | "
                  + fmt (eq, 2) + " % | " + fmt (eqWide, 2) + " % | " + fmt (ef, 2) + " % |");
        }
    check (worstG < 0.005, "the gain at the set frequency equals the set gain everywhere (worst " + fmt (worstG, 4) + " dB)");
    line ("(Q 0.5 bells with large gains are very wide; their upper half-gain point lies near 20 kHz, where the response");
    line ("necessarily departs from the analog curve, so their Q error mostly reflects the top-octave residual in the accuracy tables.)");
}

void displayMatchesDsp()
{
    line ("\n## Display versus DSP\n");
    CoherenceProcessor p;
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    int bad = 0, total = 0;
    juce::String firstBad;
    for (auto* param : p.getParameters())
    {
        auto* fp = dynamic_cast<juce::AudioParameterFloat*> (param);
        if (fp == nullptr || isMeterParameter (*fp)) continue;
        juce::RangedAudioParameter* rp = fp;
        for (int i = 0; i < 2000; ++i)
        {
            const float n = u (rng);
            const float v = rp->convertFrom0to1 (n);                       // what the DSP receives
            const auto text = rp->getText (rp->convertTo0to1 (v), 64);       // what the panel shows
            const float back = rp->convertFrom0to1 (rp->getValueForText (text));
            const bool ok = std::abs (back - v) <= 1e-5f * std::max (1.0f, std::abs (v)) || (rp->getParameterID() == id::output && v <= -59.995f && back <= -59.995f);
            ++total;
            if (! ok) { ++bad; if (firstBad.isEmpty()) firstBad = rp->getParameterID() + ": runs at " + juce::String (v, 6) + ", shows \"" + text + "\", which reads back as " + juce::String (back, 6); }
        }
    }
    check (bad == 0, "every readout shows exactly the value the DSP runs at (" + juce::String (total) + " random values over all continuous parameters"
                         + (bad ? ", first mismatch " + firstBad : juce::String()) + ")");

    // JUCE's AU wrapper asks for parameter text with a maximum length of 0, meaning no limit (v0.2.6 fix:
    // the stepped gain controls truncated to it and showed nothing in AU hosts).
    {
        int empty = 0, checked = 0;
        juce::String firstEmpty;
        for (auto* param : p.getParameters())
            for (float n : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const auto full = param->getText (n, 1024), unlimited = param->getText (n, 0);
                ++checked;
                if (unlimited != full || unlimited.isEmpty())
                    if (empty++ == 0)
                        firstEmpty = param->getName (64) + " at " + juce::String (n, 2) + ": \"" + unlimited + "\" vs \"" + full + "\"";
            }
        check (empty == 0, "getText (value, 0), as the AU wrapper asks, gives the full text for every parameter (" + juce::String (checked) + " values"
                               + (empty ? ", first difference " + firstEmpty : juce::String()) + ")");
    }

    // The Settings the engine receives carry the parameter values unchanged.
    auto& s = p.state();
    auto set = [&] (const juce::String& pid, float v) { auto* q = s.getParameter (pid); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    set ("hmf_freq", 3150.0f); set ("hmf_q", 1.41f); set ("hmf_gain", -2.37f); set (id::mid, 1.3f); set (id::side, -0.07f);
    ParameterReader reader (s);
    const auto st = reader.read();
    check (std::abs (st.bands[2].frequency - 3150.0) < 1e-3 && std::abs (st.bands[2].q - 1.41) < 1e-6 && std::abs (st.bands[2].gainDb + 2.37) < 1e-5
               && std::abs (st.midDb - 1.3) < 1e-5 && std::abs (st.sideDb + 0.07) < 1e-6,
           "parameter values reach the DSP unchanged (3150 Hz, Q 1.41, -2.37 dB, Mid +1.3 dB, Side -0.07 dB)");
}

void midSideResolution()
{
    line ("\n## Mid/Side level resolution\n");
    CoherenceProcessor p;
    auto* mid = p.state().getParameter (id::mid);
    // 0.01 dB is a legal value (fine adjustment), and every 0.1 dB step is exact.
    bool ok = true;
    for (int i = -120; i <= 120; ++i)
    {
        const float v = (float) i * 0.1f;
        const float r = mid->convertFrom0to1 (mid->convertTo0to1 (v));
        ok = ok && std::abs (r - v) < 1e-5f;
    }
    check (ok, "every 0.1 dB step from -12 to +12 dB is represented exactly");
    check (std::abs (mid->convertFrom0to1 (mid->convertTo0to1 (1.37f)) - 1.37f) < 1e-5f, "0.01 dB values are kept (fine adjustment with Shift)");
    check (mid->getDefaultValue() == mid->convertTo0to1 (0.0f), "default (double-click) is 0.0 dB");

    // DSP gain follows the value: Mid +0.1 dB on a mono-compatible signal raises the level by 0.1 dB.
    const double fs = 48000.0;
    for (double d : { 0.1, -0.1, 0.01, 1.3 })
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto s = neutral();
        s.midDb = d;
        auto x = sine ((int) fs, fs, 1000.0, 0.25f);
        auto y = run (e, s, x);
        const double meas = rmsDb (y[0], 10000) - rmsDb (x[0], 10000);
        check (std::abs (meas - d) < 1e-4, "Mid " + fmt (d, 2) + " dB measures " + fmt (meas, 5) + " dB");
    }
}

void eqStress()
{
    line ("\n## EQ review: extreme settings and stress\n");
    bool finite = true;
    double worstOverDesign = 0;
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double g : { 15.0, -15.0 })
            for (double q : { 0.3, 10.0 })
            {
                Engine e;
                e.prepare (fs, 512, 2);
                auto s = neutral();
                s.bands[0] = { Shape::bell, 16.0, g, q };
                s.bands[1] = { Shape::bell, 40.0, g, q };
                s.bands[2] = { Shape::bell, 16000.0, g, q };
                s.bands[3] = { Shape::bell, 20000.0, g, q };
                auto x = noise ((int) fs, 0.01f, 17);
                auto y = run (e, s, x);
                for (auto v : y[0]) finite = finite && std::isfinite (v);
                // the engine's output level matches the designed cascade (noise RMS through the design)
                (void) worstOverDesign;
            }
    check (finite, "four bands at +/-15 dB, Q 0.3 and Q 10, at 16 Hz, 40 Hz, 16 kHz and 20 kHz, at 44.1-192 kHz: finite and stable");

    // Random EQ automation every 32 samples for 60 s of audio: never unstable, never NaN.
    {
        const double fs = 48000.0;
        Engine e;
        e.prepare (fs, 512, 2);
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        MeterData m;
        auto x = noise (32, 0.1f, 2);
        double peak = 0;
        bool ok = true;
        for (int b = 0; b < (int) (fs * 60 / 32); ++b)
        {
            auto s = neutral();
            for (int k = 0; k < 4; ++k)
                s.bands[(size_t) k] = { k == 0 ? (u (rng) < 0.5 ? Shape::lowCut : Shape::lowShelf) : (k == 3 ? (u (rng) < 0.5 ? Shape::highCut : Shape::highShelf) : Shape::bell),
                                        16.0 * std::pow (1250.0, u (rng)), -15.0 + 30.0 * u (rng), 0.3 * std::pow (33.3, u (rng)) };
            auto y = x;
            float* ptr[2] = { y[0].data(), y[1].data() };
            e.process (ptr, 2, 32, s, m);
            for (auto v : y[0]) { ok = ok && std::isfinite (v); peak = std::max (peak, (double) std::abs (v)); }
        }
        check (ok && peak < 100.0, "random automation of all EQ parameters every 32 samples for 60 s: finite, bounded (peak " + fmt (peak, 2) + ")");
    }
}

void eqInteraction()
{
    line ("\n## EQ review: several bands together\n");
    const double fs = 48000.0;
    Engine e;
    e.prepare (fs, 512, 2);
    auto s = neutral();
    s.bands[0] = { Shape::lowShelf, 120.0, 3.5, 0.707 };
    s.bands[1] = { Shape::bell, 300.0, -4.0, 1.4 };
    s.bands[2] = { Shape::bell, 450.0, 2.5, 0.8 };    // overlapping the LMF cut
    s.bands[3] = { Shape::highShelf, 8000.0, 1.5, 0.707 };
    const int n = (int) fs;
    std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n, 0.0f));
    x[0][0] = x[1][0] = 1.0f;
    auto y = run (e, s, x);
    std::vector<double> h (y[0].begin(), y[0].end());
    double worst = 0;
    for (double f : logFreqs (20.0, 20000.0, 80))
    {
        double sum = 0;
        for (const auto& b : s.bands)
            sum += biquadMagnitudeDb (engineDesign (b.shape, b.frequency, b.gainDb, b.q, fs), f, fs);
        worst = std::max (worst, std::abs (irMagnitudeDb (h, f, fs) - sum));
    }
    check (worst < 1e-4, "four overlapping bands: measured response equals the sum of the individual band responses (max deviation " + juce::String (worst, 7) + " dB)");

    // EQ OUT is exactly the input.
    auto so = s;
    so.eqOn = false;
    Engine e2;
    e2.prepare (fs, 512, 2);
    auto xn = noise ((int) fs, 0.2f, 8);
    auto yn = run (e2, so, xn);
    check (maxAbsDiff (xn[0], yn[0]) == 0.0, "EQ OUT passes the input bit for bit");
}

// Host-style automation (a new value every 512-sample block) on a 1 kHz sine; energy away from 1 kHz (the
// zipper noise), for COHERENCE and for the same filters updated without smoothing.
void eqZipper()
{
    line ("\n## EQ review: smoothness under automation\n");
    line ("1 kHz sine through a bell, the parameter changed by the host every 512 samples (a 2 s ramp in steps).");
    line ("Energy more than 300 Hz away from 1 kHz, relative to the signal (lower is cleaner):\n");
    line ("| Automated parameter | COHERENCE (smoothed) | Same filter, no smoothing |");
    line ("|---|---|---|");
    const double fs = 48000.0;
    const int n = (int) (fs * 2);
    auto spur = [&] (const std::vector<double>& y)
    {
        // DFT on a coarse grid excluding 700..1300 Hz, Hann window
        double out = 0, tot = 0;
        std::vector<double> w (y.size());
        for (size_t i = 0; i < y.size(); ++i)
            w[i] = y[i] * (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (y.size() - 1)));
        for (double f = 50.0; f < 20000.0; f += 25.0)
        {
            std::complex<double> acc = 0;
            const double wr = -2.0 * juce::MathConstants<double>::pi * f / fs;
            for (size_t i = 0; i < w.size(); i += 1)
                acc += w[i] * std::polar (1.0, wr * (double) i);
            const double p = std::norm (acc);
            tot += p;
            if (std::abs (f - 1000.0) > 300.0) out += p;
        }
        return 10.0 * std::log10 (out / tot + 1e-300);
    };
    double worstSmooth = -1e9;
    for (int which = 0; which < 3; ++which)
    {
        auto paramAt = [&] (int block, double& f0, double& g, double& q)
        {
            const double t = (double) block * 512.0 / (double) n;
            f0 = 1000.0; g = 6.0; q = 1.0;
            if (which == 0) g = -12.0 + 24.0 * t;
            if (which == 1) f0 = 500.0 * std::pow (4.0, t);
            if (which == 2) q = 0.5 * std::pow (16.0, t);
        };
        // COHERENCE
        Engine e;
        e.prepare (fs, 512, 2);
        auto x = sine (n, fs, 1000.0, 0.25f);
        auto y = x;
        MeterData m;
        for (int start = 0, blk = 0; start < n; start += 512, ++blk)
        {
            auto s = neutral();
            double f0, g, q;
            paramAt (blk, f0, g, q);
            s.bands[2] = { Shape::bell, f0, g, q };
            float* p[2] = { y[0].data() + start, y[1].data() + start };
            e.process (p, 2, std::min (512, n - start), s, m);
        }
        // Reference: coefficients jump at every block boundary.
        std::vector<double> ref ((size_t) n);
        SvfState st;
        SvfCoeffs c {};
        for (int i = 0; i < n; ++i)
        {
            if (i % 512 == 0)
            {
                double f0, g, q;
                paramAt (i / 512, f0, g, q);
                c = coeffsFromSvf (designBand (Shape::bell, f0, g, bellDesignQ (q, g), fs));
            }
            ref[(size_t) i] = svfTick (c, st, (double) x[0][(size_t) i]);
        }
        const double a = spur (std::vector<double> (y[0].begin() + 4800, y[0].end() - 4800));
        const double b = spur (std::vector<double> (ref.begin() + 4800, ref.end() - 4800));
        worstSmooth = std::max (worstSmooth, a);
        line (juce::String ("| ") + (which == 0 ? "Gain -12 to +12 dB" : (which == 1 ? "Frequency 500 Hz to 2 kHz" : "Q 0.5 to 8")) + " | "
              + fmt (a, 1) + " dB | " + fmt (b, 1) + " dB |");
    }
    line ("");
    check (worstSmooth < -90.0, "automation produces no audible zipper noise (worst " + fmt (worstSmooth, 1) + " dB relative to the signal)");
}

void cpuBreakdown()
{
    line ("\n## CPU by section (48 kHz stereo, one instance)\n");
    const double fs = 48000.0;
    auto timeIt = [&] (Settings s)
    {
        Engine e;
        e.prepare (fs, 256, 2);
        auto x = noise (256, 0.2f);
        MeterData m;
        const int blocks = (int) (fs * 20 / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            auto y = x;
            float* p[2] = { y[0].data(), y[1].data() };
            e.process (p, 2, 256, s, m);
        }
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / (20.0 * fs) * 1e9;
    };
    auto s = neutral();
    s.eqOn = false;
    const double base = timeIt (s);
    s.eqOn = true;
    s.bands[2].gainDb = 3.0;
    const double eq = timeIt (s);
    s.dynOn = true;
    const double eqDyn = timeIt (s);
    s.sideDb = 1.0;
    const double all = timeIt (s);
    s.msEq = true;
    s.sideBands = s.bands;
    const double ms = timeIt (s);
    line ("- I/O, meters, both orders' EQ running (EQ out): " + fmt (base, 1) + " ns per stereo sample");
    line ("- EQ in: " + fmt (eq, 1) + " ns");
    line ("- EQ and compressor in: " + fmt (eqDyn, 1) + " ns");
    line ("- everything (incl. Mid/Side): " + fmt (all, 1) + " ns (" + fmt (all * fs * 1e-9 * 100.0, 2) + " % of one core)");
    line ("- with M/S EQ: " + fmt (ms, 1) + " ns (" + fmt (ms * fs * 1e-9 * 100.0, 2) + " % of one core)");
}

void transparency()
{
    line ("\n## Transparency and signal flow\n");
    for (double fs : { 44100.0, 96000.0 })
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto x = noise ((int) fs, 0.2f);
        auto y = run (e, neutral(), x);
        const double d = std::max (maxAbsDiff (x[0], y[0]), maxAbsDiff (x[1], y[1]));
        check (d < 1.0e-6, "default settings are transparent at " + fmt (fs / 1000.0, 1) + " kHz (max sample difference " + juce::String (d, 9) + ")");
    }

    // Order: EQ -> DYN equals the EQ alone followed by the compressor alone, and differs from DYN -> EQ.
    {
        const double fs = 48000.0;
        auto base = neutral();
        base.bands[1] = { Shape::bell, 200.0, 9.0, 0.8 };
        base.bands[3] = { Shape::highShelf, 6000.0, 6.0, 0.71 };
        base.comp = { -24.0, 4.0, 3.0, 120.0, false, 0.3, 0.0, false, 1.0, 0.0 };
        auto x = noise ((int) fs * 2, 0.15f, 3);

        auto eqOnly = base;   eqOnly.dynOn = false;
        auto dynOnly = base;  dynOnly.eqOn = false; dynOnly.dynOn = true;
        auto a = base;        a.dynOn = true; a.compFirst = false;
        auto b = base;        b.dynOn = true; b.compFirst = true;

        Engine e1, e2, e3, e4, e5, e6;
        for (auto* e : { &e1, &e2, &e3, &e4, &e5, &e6 }) e->prepare (fs, 512, 2);
        auto yA = run (e3, a, x);
        auto ref = run (e2, dynOnly, run (e1, eqOnly, x));
        auto yB = run (e4, b, x);
        auto refB = run (e6, eqOnly, run (e5, dynOnly, x));
        const double dA = maxAbsDiff (yA[0], ref[0], 1000), dB = maxAbsDiff (yB[0], refB[0], 1000), dAB = maxAbsDiff (yA[0], yB[0], 1000);
        check (dA < 1e-5, "EQ > DYN equals EQ then compressor (max difference " + juce::String (dA, 8) + ")");
        check (dB < 1e-5, "DYN > EQ equals compressor then EQ (max difference " + juce::String (dB, 8) + ")");
        check (dAB > 1e-2, "the two orders really differ (max difference " + fmt (dAB, 4) + ")");
    }

    // Order switch while playing: no discontinuity.
    {
        const double fs = 48000.0;
        Engine e;
        e.prepare (fs, 512, 2);
        auto s = neutral();
        s.bands[1] = { Shape::bell, 200.0, 9.0, 0.8 };
        s.dynOn = true;
        s.comp = { -30.0, 6.0, 1.0, 80.0, false, 0.2, 6.0, false, 1.0, 0.0 };
        const int len = (int) fs * 2;
        auto x = sine (len, fs, 220.0, 0.5f);
        MeterData m;
        auto y = x;
        double maxStep = 0, maxStepBefore = 0, maxStepAfter = 0;
        for (int start = 0, blk = 0; start < len; start += 256, ++blk)
        {
            s.compFirst = blk >= 100;
            float* p[2] = { y[0].data() + start, y[1].data() + start };
            e.process (p, 2, std::min (256, len - start), s, m);
        }
        for (size_t i = 2; i < y[0].size(); ++i)
        {
            // second difference: a click shows up as a spike
            const double d2 = std::abs (y[0][i] - 2 * y[0][i - 1] + y[0][i - 2]);
            if (i > 100 * 256 - 2000 && i < 100 * 256 + 3000) maxStep = std::max (maxStep, d2);
            else if (i > 20000 && i < 100 * 256 - 2000) maxStepBefore = std::max (maxStepBefore, d2);
            else if (i > 100 * 256 + 20000) maxStepAfter = std::max (maxStepAfter, d2);
        }
        const double steady = std::max (maxStepBefore, maxStepAfter);
        check (maxStep < steady * 1.1, "switching order while playing is click-free (max 2nd difference around the switch " + fmt (maxStep, 5)
                                           + ", steady state before/after " + fmt (maxStepBefore, 5) + " / " + fmt (maxStepAfter, 5) + ")");
    }
}

void architecture()
{
    line ("\n## Architecture: Mid/Side, balance, mono\n");
    const double fs = 48000.0;
    const int n = (int) fs;
    auto x = sine (n, fs, 500.0, 0.5f, 0.1f);   // L and R different, same phase

    auto expect = [&] (double midDb, double sideDb, double bal, double& outL, double& outR)
    {
        const double gm = std::pow (10.0, midDb / 20), gs = std::pow (10.0, sideDb / 20);
        const double M = 0.5 * (0.5 + 0.1) * gm, S = 0.5 * (0.5 - 0.1) * gs;
        outL = (M + S) * (bal > 0 ? 1 - bal : 1.0);
        outR = (M - S) * (bal < 0 ? 1 + bal : 1.0);
    };
    auto measure = [&] (Settings s)
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto y = run (e, s, x);
        const double pk = std::pow (10.0, rmsDb (y[0], 5000) / 20.0) * std::sqrt (2.0);
        const double pr = std::pow (10.0, rmsDb (y[1], 5000) / 20.0) * std::sqrt (2.0);
        return std::pair { pk, pr };
    };
    for (auto [md, sd, bal] : { std::tuple { 0.0, 6.0, 0.0 }, std::tuple { -3.0, 0.0, 0.0 }, std::tuple { 2.0, -12.0, 0.0 }, std::tuple { 0.0, 0.0, 0.5 } })
    {
        auto s = neutral();
        s.midDb = md; s.sideDb = sd; s.balance = bal;
        auto [l, r] = measure (s);
        double el, er;
        expect (md, sd, bal, el, er);
        const bool ok = std::abs (l - std::abs (el)) < 1e-3 && std::abs (r - std::abs (er)) < 1e-3;   // RMS-derived amplitudes
        check (ok, "Mid " + fmt (md, 0) + " dB, Side " + fmt (sd, 0) + " dB, balance " + fmt (bal, 1) + ": L " + fmt (l, 4) + " (expected " + fmt (el, 4)
                       + "), R " + fmt (r, 4) + " (expected " + fmt (std::abs (er), 4) + ")");
    }

    // Same M/S result in both orders (architecture is after EQ and dynamics either way).
    {
        auto s = neutral();
        s.sideDb = 4.0; s.midDb = -1.5;
        s.bands[2] = { Shape::bell, 2000.0, 5.0, 1.0 };
        s.dynOn = true;
        s.comp = { -20.0, 3.0, 5.0, 100.0, false, 0.4, 0.0, false, 1.0, 0.0 };
        auto xx = noise (n, 0.2f, 11);
        Engine a, b, c, d;
        for (auto* e : { &a, &b, &c, &d }) e->prepare (fs, 512, 2);
        auto noArch = s; noArch.sideDb = 0; noArch.midDb = 0;
        auto yArch = run (a, s, xx);
        auto yNo = run (b, noArch, xx);
        // apply M/S by hand to the un-architected output
        double worst = 0;
        const double gm = std::pow (10.0, -1.5 / 20), gs = std::pow (10.0, 4.0 / 20);
        for (size_t i = 2000; i < yNo[0].size(); ++i)
        {
            const double M = 0.5 * (yNo[0][i] + yNo[1][i]) * gm, S = 0.5 * (yNo[0][i] - yNo[1][i]) * gs;
            worst = std::max (worst, std::abs ((M + S) - yArch[0][i]));
        }
        check (worst < 1e-5, "Mid/Side is applied after EQ and dynamics (max deviation " + juce::String (worst, 8) + ")");
    }

    // Mono: output is the mono sum, the processing and the meters are untouched.
    {
        auto s = neutral();
        s.sideDb = 3.0;
        auto sm = s; sm.mono = true;
        Engine a, b;
        a.prepare (fs, 512, 2);
        b.prepare (fs, 512, 2);
        MeterData ma, mb;
        auto ya = x, yb = x;
        for (int start = 0; start < n; start += 256)
        {
            float* pa[2] = { ya[0].data() + start, ya[1].data() + start };
            float* pb[2] = { yb[0].data() + start, yb[1].data() + start };
            const int len = std::min (256, n - start);
            a.process (pa, 2, len, s, ma);
            b.process (pb, 2, len, sm, mb);
        }
        double worst = 0;
        for (size_t i = 5000; i < ya[0].size(); ++i)
            worst = std::max ({ worst, std::abs (0.5 * (ya[0][i] + ya[1][i]) - yb[0][i]), std::abs (0.5 * (ya[0][i] + ya[1][i]) - yb[1][i]) });
        check (worst < 1e-6, "MONO outputs (L+R)/2 of the stereo result");
        check (std::abs (ma.outRms[0].load() - mb.outRms[0].load()) < 1e-6 && std::abs (ma.sideRms.load() - mb.sideRms.load()) < 1e-6,
               "MONO leaves the meters (L/R and M/S) showing the stereo signal");
    }
}

// AUTO makeup: a share of the level the compressor measurably takes away (DESIGN.md section 16).
void autoMakeupChecks (double fs)
{
    const auto settings = [] (double thr, double ratio, double att, double rel, bool automakeup)
    {
        auto s = neutral();
        s.dynOn = true;
        s.comp = { thr, ratio, att, rel, false, 0.4, 0.0, automakeup, 1.0, 0.0 };
        return s;
    };
    const auto meanDb = [] (const std::vector<float>& v, size_t from, size_t to)
    {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i)
            sum += (double) v[i] * (double) v[i];
        return 10.0 * std::log10 (sum / (double) (to - from) + 1e-30);
    };
    // Noise with a 3 Hz level movement: reduction comes and goes, like program material.
    auto program = noise ((int) fs * 12, 0.3f, 21);
    for (auto& ch : program)
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i] *= (float) (0.55 + 0.45 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * (double) i / fs));
    const auto n = program[0].size();

    // Below the threshold there is no makeup at all.
    {
        Engine on, off;
        on.prepare (fs, 512, 2);
        off.prepare (fs, 512, 2);
        auto x = noise ((int) fs * 4, 0.05f, 22);
        auto a = run (on, settings (-6.0, 4.0, 10.0, 150.0, true), x);
        auto b = run (off, settings (-6.0, 4.0, 10.0, 150.0, false), x);
        check (maxAbsDiff (a[0], b[0]) == 0.0 && maxAbsDiff (a[0], x[0]) < 1e-6,
               "AUTO makeup adds nothing to a signal below the threshold: identical to AUTO off");
    }

    // A steady tone: the level taken away is known, so the share restored is too.
    {
        Engine on, off;
        on.prepare (fs, 512, 2);
        off.prepare (fs, 512, 2);
        auto s = settings (-30.0, 4.0, 1.0, 100.0, true);
        s.comp.knee = 0.0;
        auto x = sine ((int) fs * 8, fs, 1000.0, 0.5f);
        auto y = run (on, s, x);
        s.comp.autoMakeup = false;
        auto z = run (off, s, x);
        const double net = rmsDb (y[0], (size_t) fs * 6) - rmsDb (x[0], (size_t) fs * 6), loss = rmsDb (x[0], (size_t) fs * 6) - rmsDb (z[0], (size_t) fs * 6);
        check (std::abs (net + 0.2 * loss) < 0.02, "AUTO makeup restores 80 % of the measured loss on a steady tone (" + fmt (loss, 2) + " dB lost, " + fmt (net, 2) + " dB net)");
    }

    // It never ends up louder than the input, at any of these settings; manual Makeup comes on top.
    {
        double loudest = -100.0, quietest = 0.0;
        for (const double thr : { -14.0, -24.0, -36.0 })
            for (const double att : { 1.0, 10.0, 30.0 })
                for (const double ratio : { 2.0, 4.0, 100.0 })
                {
                    Engine e;
                    e.prepare (fs, 512, 2);
                    auto y = run (e, settings (thr, ratio, att, 150.0, true), program);
                    const double d = rmsDb (y[0], (size_t) fs * 6) - rmsDb (program[0], (size_t) fs * 6);
                    loudest = std::max (loudest, d);
                    quietest = std::min (quietest, d);
                }
        check (loudest < 0.0 && quietest > -6.0, "with AUTO makeup the output stays under the input over 27 settings (" + fmt (quietest, 2) + " to " + fmt (loudest, 2) + " dB)");

        Engine a, b;
        a.prepare (fs, 512, 2);
        b.prepare (fs, 512, 2);
        auto s = settings (-24.0, 4.0, 10.0, 150.0, true);
        auto y0 = run (a, s, program);
        s.comp.makeupDb = 3.0;
        auto y3 = run (b, s, program);
        const double d = rmsDb (y3[0], (size_t) fs * 6) - rmsDb (y0[0], (size_t) fs * 6);
        check (std::abs (d - 3.0) < 0.01, "Makeup adds on top of AUTO makeup (" + fmt (d, 3) + " dB for 3 dB)");
    }

    // The compressor itself doesn't change: the difference from AUTO off is a slow gain.
    {
        Engine on, off;
        on.prepare (fs, 512, 2);
        off.prepare (fs, 512, 2);
        auto a = run (on, settings (-24.0, 4.0, 10.0, 150.0, true), program);
        auto b = run (off, settings (-24.0, 4.0, 10.0, 150.0, false), program);
        const size_t w = (size_t) (0.1 * fs);
        double fastest = 0.0, previous = 0.0;
        for (size_t i = (size_t) fs * 2; i + w <= n; i += w)
        {
            const double g = meanDb (a[0], i, i + w) - meanDb (b[0], i, i + w);
            if (i > (size_t) fs * 2)
                fastest = std::max (fastest, std::abs (g - previous));
            previous = g;
        }
        check (fastest < 0.25, "AUTO makeup is a slow gain after the compressor: at most " + fmt (fastest, 3) + " dB per 100 ms on moving material");
    }

    // A quiet passage below the threshold: the makeup goes, so it isn't left louder than it came in.
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto x = noise ((int) fs * 18, 0.3f, 23);
        const size_t drop = (size_t) fs * 8;
        for (auto& ch : x)
            for (size_t i = drop; i < ch.size(); ++i)
                ch[i] *= 0.01f;
        auto y = run (e, settings (-24.0, 4.0, 10.0, 150.0, true), x);
        const size_t w = (size_t) (0.5 * fs);
        const auto extra = [&] (double t) { const auto i = drop + (size_t) (t * fs); return meanDb (y[0], i, i + w) - meanDb (x[0], i, i + w); };
        check (extra (1.0) > 1.0 && extra (6.0) < 0.5 && extra (9.0) < 0.05,
               "after loud material, a signal below the threshold returns to unity: +" + fmt (extra (1.0), 2) + " dB after 1 s, +" + fmt (extra (6.0), 2) + " dB after 6 s, +" + fmt (extra (9.0), 3) + " dB after 9 s");
    }

    // Moving the threshold: the level comes back quickly. Switching the compressor in: already settled.
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto x = noise ((int) fs * 8, 0.3f, 24);
        auto s = settings (-20.0, 4.0, 10.0, 150.0, true);
        run (e, s, x);
        s.comp.thresholdDb = -36.0;
        auto y = run (e, s, x);
        const auto level = [&] (double t) { const auto i = (size_t) (t * fs); return meanDb (y[0], i, i + (size_t) (0.25 * fs)) - meanDb (x[0], i, i + (size_t) (0.25 * fs)); };
        check (std::abs (level (1.5) - level (7.0)) < 0.5, "after a threshold move AUTO makeup settles within 1.5 s (" + fmt (level (1.5), 2) + " dB, final " + fmt (level (7.0), 2) + " dB)");

        Engine sw;
        sw.prepare (fs, 512, 2);
        auto t = settings (-24.0, 4.0, 10.0, 150.0, true);
        t.dynOn = false;
        run (sw, t, x);
        t.dynOn = true;
        auto z = run (sw, t, x);
        const auto after = [&] (double from) { const auto i = (size_t) (from * fs); return meanDb (z[0], i, i + (size_t) (0.25 * fs)) - meanDb (x[0], i, i + (size_t) (0.25 * fs)); };
        check (std::abs (after (0.05) - after (7.0)) < 0.25, "switching the compressor in with AUTO makeup is already at level (" + fmt (after (0.05), 2) + " dB at once, " + fmt (after (7.0), 2) + " dB later)");
    }

    // Parallel: the makeup stays in the wet path, and the sum stays under the input.
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto s = settings (-24.0, 4.0, 10.0, 150.0, true);
        s.comp.mix = 0.5;
        auto y = run (e, s, program);
        const double d = rmsDb (y[0], (size_t) fs * 6) - rmsDb (program[0], (size_t) fs * 6);
        check (d < 0.0 && d > -3.0, "with MIX at 50 % and AUTO makeup the output stays under the input (" + fmt (d, 2) + " dB)");
    }
}

void compressor()
{
    line ("\n## Compressor\n");
    line ("v0.3.0: a model of FabFilter Pro-C 3 (Clean). Reference values below were measured on Pro-C 3 v3.00 with");
    line ("Tools/plughost (mac-results/proc3-clean/, COMPRESSOR-TESTING.md); the full comparison is Tools/proc3/compare.py.\n");
    const double fs = 48000.0;
    // Static curve on DC (the detector reads the level exactly), then on a sine.
    line ("Static curve (threshold -20 dB, ratio 4:1) on DC levels, output level change vs. the static curve:\n");
    line ("| Knee | Input | Measured gain | Static curve |");
    line ("|---|---|---|---|");
    double worst = 0;
    for (double knee : { 0.0, 0.4, 0.7 })
        for (double lvl : { -30.0, -24.0, -20.0, -15.0, -10.0, -5.0, 0.0 })
        {
            Engine e;
            e.prepare (fs, 512, 2);
            auto s = neutral();
            s.dynOn = true;
            s.comp = { -20.0, 4.0, 1.0, 100.0, false, knee, 0.0, false, 1.0, 0.0 };
            std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) fs * 2, (float) std::pow (10.0, lvl / 20.0)));
            auto y = run (e, s, x);
            const double meas = 20.0 * std::log10 (std::abs ((double) y[0].back()) / std::abs ((double) x[0].back()));
            const double theory = Compressor::staticGainDb (lvl, -20.0, 0.75, kneeWidthDb (knee));
            worst = std::max (worst, std::abs (meas - theory));
            line ("| " + fmt (kneeWidthDb (knee), 1) + " dB | " + fmt (lvl, 0) + " dBFS | " + fmt (meas, 3) + " dB | " + fmt (theory, 3) + " dB |");
        }
    check (worst < 0.01, "steady gain on DC follows the static curve (ratio, knee) within 0.01 dB (worst " + fmt (worst, 4) + " dB)");
    {
        // Pro-C 3 Clean, 1 kHz sine peaking 10 dB over a -20 dB threshold, 4:1, knee 0, 10/150 ms: 6.09 dB (rms).
        Engine e;
        e.prepare (fs, 512, 2);
        auto s = neutral();
        s.dynOn = true;
        s.comp = { -20.0, 4.0, 10.0, 150.0, false, 0.0, 0.0, false, 1.0, 0.0 };
        auto x = sine ((int) fs * 3, fs, 1000.0, (float) std::pow (10.0, -10.0 / 20.0));
        auto y = run (e, s, x);
        const double g = rmsDb (y[0], (size_t) fs * 2) - rmsDb (x[0], (size_t) fs * 2);
        check (std::abs (g + 6.09) < 0.15, "sine 10 dB over threshold (10/150 ms): " + fmt (-g, 2) + " dB of reduction (Pro-C 3 Clean: 6.09 dB); the detector reads the instantaneous peak, so a sine reads below its peak");
    }

    // Attack and release trajectories on DC steps (-40 dBFS -> 10 dB over a -20 dB threshold, 4:1, knee 0).
    auto step = [&] (double att, double rel, double seconds)
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto s = neutral();
        s.dynOn = true;
        s.comp = { -20.0, 4.0, att, rel, false, 0.0, 0.0, false, 1.0, 0.0 };
        const int pre = (int) (0.2 * fs), on = (int) (seconds * fs), n = pre + on + (int) fs * 3;
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n, 0.01f));
        for (int i = pre; i < pre + on; ++i)
            x[0][(size_t) i] = x[1][(size_t) i] = 0.316228f;
        auto y = run (e, s, x, 64);
        std::vector<double> gr ((size_t) n);
        for (int i = 0; i < n; ++i)
            gr[(size_t) i] = -20.0 * std::log10 ((double) y[0][(size_t) i] / (double) x[0][(size_t) i]);
        const double end = gr[(size_t) (pre + on - 1)];
        double tA = -1, tR = -1;
        for (int i = pre; i < pre + on; ++i) if (gr[(size_t) i] >= 0.632 * 7.5) { tA = (i - pre) / fs * 1000.0; break; }
        for (int i = pre + on; i < n; ++i) if (gr[(size_t) i] <= 0.368 * end) { tR = (i - pre - on) / fs * 1000.0; break; }
        return std::pair { tA, tR };
    };
    line ("\nDC steps, 10 dB over the threshold (7.5 dB of reduction), times to 63 % of the attack and to 37 % in release, against Pro-C 3 Clean:\n");
    line ("| Attack / release, burst | Attack 63 % | Pro-C 3 | Release to 37 % | Pro-C 3 |");
    line ("|---|---|---|---|---|");
    bool ok = true;
    for (auto [att, rel, sec, pa, pr] : { std::tuple { 1.0, 100.0, 0.01, 0.81, 40.3 }, std::tuple { 10.0, 100.0, 0.4, 4.71, 54.3 },
                                          std::tuple { 1.0, 100.0, 2.0, 0.81, 156.8 }, std::tuple { 1.0, 1000.0, 0.01, 0.81, 353.3 },
                                          std::tuple { 100.0, 100.0, 0.4, 43.3, 22.0 } })
    {
        const auto [tA, tR] = step (att, rel, sec);
        ok = ok && tA > 0 && tR > 0 && std::abs (tA - pa) < std::max (0.25, 0.1 * pa) && std::abs (tR - pr) < 0.15 * pr;
        line ("| " + fmt (att, 0) + " / " + fmt (rel, 0) + " ms, " + fmt (sec * 1000.0, 0) + " ms | " + fmt (tA, 2) + " ms | " + fmt (pa, 2) + " ms | "
              + fmt (tR, 1) + " ms | " + fmt (pr, 1) + " ms |");
    }
    check (ok, "attack within 10 % and release within 15 % of Pro-C 3 Clean (distance-dependent attack, program-dependent release)");
    {
        // The meter shows the reduction actually applied.
        MeterData m;
        Engine e2;
        e2.prepare (fs, 512, 2);
        auto s2 = neutral(); s2.dynOn = true;
        s2.comp = { -20.0, 4.0, 5.0, 150.0, false, 0.0, 0.0, false, 1.0, 0.0 };
        std::vector<std::vector<float>> xs (2, std::vector<float> ((size_t) fs, 0.316228f));
        auto ys = xs;
        for (int start = 0; start < (int) fs; start += 512)
        {
            float* ptr[2] = { ys[0].data() + start, ys[1].data() + start };
            m.gainReductionDb.store (0.0f);
            e2.process (ptr, 2, std::min (512, (int) fs - start), s2, m);
        }
        const double metered = m.gainReductionDb.load(), applied = 20.0 * std::log10 ((double) ys[0].back() / (double) xs[0].back());
        check (std::abs (metered - applied) < 0.01, "the gain-reduction meter shows the reduction applied (" + fmt (metered, 3) + " dB metered, " + fmt (applied, 3) + " dB measured)");
    }

    autoMakeupChecks (fs);

    // Mix: 0 % is the dry signal.
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto s = neutral();
        s.dynOn = true;
        s.comp = { -40.0, 10.0, 1.0, 100.0, false, 0.0, 6.0, false, 0.0, 0.0 };
        auto x = noise ((int) fs, 0.2f, 5);
        auto y = run (e, s, x);
        check (maxAbsDiff (x[0], y[0], 5000) < 1e-6, "MIX 0% passes the uncompressed signal");
    }
}

void eqOutputChecks()
{
    line ("\n## EQ Output\n");
    const double fs = 48000.0;
    auto x = noise ((int) fs * 2, 0.3f, 31);
    for (auto& ch : x)
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i] *= (float) (0.55 + 0.45 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * (double) i / fs));

    const auto render = [&] (bool compFirst, bool eqOn, double eqOutDb, bool dynOn)
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto s = neutral();
        s.eqOn = eqOn;
        s.bands[1].gainDb = 3.0;
        s.dynOn = dynOn;
        s.compFirst = compFirst;
        s.comp.thresholdDb = -24.0;
        s.comp.autoMakeup = false;
        s.eqOutputDb = eqOutDb;
        return run (e, s, x);
    };

    // Exactly a gain after everything else, in both orders: the compressor hears the same.
    double worst = 0.0;
    for (const bool compFirst : { false, true })
        for (const double db : { -6.0, 6.0 })
        {
            const auto a = render (compFirst, true, db, true), b = render (compFirst, true, 0.0, true);
            const double g = std::pow (10.0, db / 20.0);
            for (size_t i = 0; i < a[0].size(); ++i)
                worst = std::max (worst, std::abs ((double) a[0][i] - g * (double) b[0][i]));
        }
    check (worst < 1e-5, "EQ Output is only a gain after the compressor, in both orders: the compressor's output is unchanged (worst difference " + fmt (20.0 * std::log10 (worst + 1e-12), 0) + " dBFS)");

    // It follows EQ IN, with no click.
    {
        const auto a = render (false, false, 6.0, false), b = render (false, false, 0.0, false);
        check (maxAbsDiff (a[0], b[0]) == 0.0 && maxAbsDiff (a[0], x[0]) == 0.0, "with the EQ out, EQ Output does nothing: the input passes bit for bit");

        Engine e;
        e.prepare (fs, 512, 2);
        auto s = neutral();
        s.eqOutputDb = 6.0;
        auto tone = sine ((int) fs, fs, 200.0, 0.25f);
        auto half = tone;
        run (e, s, tone);
        s.eqOn = false;
        auto y = run (e, s, tone);
        double step = 0.0;
        for (size_t i = 1; i < y[0].size(); ++i)
            step = std::max (step, std::abs ((double) y[0][i] - (double) y[0][i - 1]));
        const double clean = 2.0 * 0.5 * std::sin (juce::MathConstants<double>::pi * 200.0 / fs);   // the same tone at +6 dB
        check (step < 1.1 * clean, "EQ IN takes EQ Output out without a click (largest sample step " + fmt (step, 4) + ", the tone's own " + fmt (clean, 4) + ")");
    }

    // Recall: a set from before it existed loads at 0 dB, even over an instance where it was moved.
    {
        CoherenceProcessor p;
        auto* q = p.state().getParameter (id::eqOutput);
        q->setValueNotifyingHost (q->convertTo0to1 (4.0f));
        juce::MemoryBlock withIt;
        p.getStateInformation (withIt);
        auto xml = juce::AudioProcessor::getXmlFromBinary (withIt.getData(), (int) withIt.getSize());
        std::vector<juce::XmlElement*> gone;
        for (auto* c : xml->getChildWithTagNameIterator ("PARAM"))
            if (c->getStringAttribute ("id") == id::eqOutput)
                gone.push_back (c);
        for (auto* c : gone)
            xml->removeChildElement (c, true);
        if (auto* other = xml->getChildByName ("OTHER_SLOT"))
            other->deleteAllChildElements();
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        CoherenceProcessor over;
        auto* r = over.state().getParameter (id::eqOutput);
        r->setValueNotifyingHost (r->convertTo0to1 (-5.0f));
        over.setStateInformation (old.getData(), (int) old.getSize());
        check (std::abs (over.state().getRawParameterValue (id::eqOutput)->load()) < 1e-4f, "a set from before EQ Output existed loads it at 0 dB, even over an instance where it was moved");

        CoherenceProcessor back;
        back.setStateInformation (withIt.getData(), (int) withIt.getSize());
        check (std::abs (back.state().getRawParameterValue (id::eqOutput)->load() - 4.0f) < 1e-3f, "EQ Output is saved and recalled");

        int last = -1, meter = -1, index = 0;
        for (auto* pp : p.getParameters())
        {
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (pp))
            {
                if (rp->getParameterID() == id::eqOutput) last = index;
                if (isMeterParameter (*rp)) meter = index;
            }
            ++index;
        }
        check (last > meter && meter == 36, "EQ Output comes after the meter (index " + juce::String (last) + "), so that no existing index moves except for the removal of AUTO GAIN");
    }
}

void msEqChecks()
{
    line ("\n## M/S EQ\n");
    const auto stereoNoise = [] (double fs)
    {
        auto x = noise ((int) fs, 0.2f, 41);
        x[1] = noise ((int) fs, 0.15f, 42)[0];
        return x;
    };
    const auto base = [] (bool weiss)
    {
        auto s = neutral();
        s.weissEq = weiss;
        s.sideBands = s.bands;
        return s;
    };

    double worstFlat = 0.0, worstMatch = 0.0;
    for (const double fs : { 48000.0, 96000.0 })
        for (const bool weiss : { false, true })
        {
            const auto x = stereoNoise (fs);

            // Flat M/S nulls against flat stereo.
            {
                Engine a, b;
                a.prepare (fs, 512, 2);
                b.prepare (fs, 512, 2);
                auto s = base (weiss);
                const auto stereo = run (b, s, x);
                s.msEq = true;
                const auto ms = run (a, s, x);
                worstFlat = std::max ({ worstFlat, maxAbsDiff (ms[0], stereo[0]), maxAbsDiff (ms[1], stereo[1]) });
            }

            // Mid and Side each get exactly what a plain EQ gives that channel: encode, process, decode, independently.
            auto s = base (weiss);
            s.bands[1] = { Shape::bell, 1000.0, 6.0, 1.0 };
            s.sideBands[2] = { Shape::bell, 3000.0, -4.0, 1.4 };
            s.sideBands[3] = { Shape::highShelf, 8000.0, 3.0, 0.71 };
            auto mid = x, side = x;
            for (size_t i = 0; i < x[0].size(); ++i)
            {
                mid[0][i] = mid[1][i] = 0.5f * (x[0][i] + x[1][i]);
                side[0][i] = side[1][i] = 0.5f * (x[0][i] - x[1][i]);
            }
            Engine em, es, e;
            em.prepare (fs, 512, 2);
            es.prepare (fs, 512, 2);
            e.prepare (fs, 512, 2);
            auto sm = base (weiss), ss = base (weiss);
            sm.bands = s.bands;
            ss.bands = s.sideBands;
            const auto ym = run (em, sm, mid), ys = run (es, ss, side);
            s.msEq = true;
            const auto y = run (e, s, x);
            for (size_t i = 0; i < y[0].size(); ++i)
                worstMatch = std::max ({ worstMatch, std::abs ((double) y[0][i] - ((double) ym[0][i] + (double) ys[0][i])),
                                         std::abs ((double) y[1][i] - ((double) ym[0][i] - (double) ys[0][i])) });
        }
    check (worstFlat < 2e-6, "flat M/S nulls against flat stereo, both EQ modes, 48 and 96 kHz (worst " + fmt (20.0 * std::log10 (worstFlat + 1e-12), 0) + " dBFS)");
    check (worstMatch < 2e-6, "M/S: L = Mid + Side, R = Mid - Side, each channel exactly as a plain EQ treats it, both EQ modes, 48 and 96 kHz (worst " + fmt (20.0 * std::log10 (worstMatch + 1e-12), 0) + " dBFS)");

    const double fs = 48000.0;
    const auto x = stereoNoise (fs);

    // The stereo path doesn't know about Side.
    {
        Engine a, b;
        a.prepare (fs, 512, 2);
        b.prepare (fs, 512, 2);
        auto s = base (false);
        s.bands[1].gainDb = 3.0;
        const auto clean = run (a, s, x);
        s.sideBands[1].gainDb = 12.0;
        const auto y = run (b, s, x);
        check (maxAbsDiff (y[0], clean[0]) == 0.0 && maxAbsDiff (y[1], clean[1]) == 0.0, "in stereo the Side settings do nothing: bit for bit the same");
    }

    // Stereo compatibility: Side moves leave the mono sum alone; Mid moves leave the difference alone.
    {
        Engine a, b;
        a.prepare (fs, 512, 2);
        b.prepare (fs, 512, 2);
        auto s = base (false);
        s.msEq = true;
        s.sideBands[1] = { Shape::bell, 800.0, 9.0, 1.0 };
        const auto ys = run (a, s, x);
        s.sideBands[1].gainDb = 0.0;
        s.bands[1] = { Shape::bell, 800.0, 9.0, 1.0 };
        const auto ym = run (b, s, x);
        double sum = 0.0, diff = 0.0;
        for (size_t i = 0; i < x[0].size(); ++i)
        {
            sum = std::max (sum, std::abs (0.5 * ((double) ys[0][i] + (double) ys[1][i]) - 0.5 * ((double) x[0][i] + (double) x[1][i])));
            diff = std::max (diff, std::abs (0.5 * ((double) ym[0][i] - (double) ym[1][i]) - 0.5 * ((double) x[0][i] - (double) x[1][i])));
        }
        check (sum < 2e-6 && diff < 2e-6, "a Side move leaves the mono sum unchanged and a Mid move leaves the difference unchanged (worst " + fmt (20.0 * std::log10 (std::max (sum, diff) + 1e-12), 0) + " dBFS)");
    }

    // Both orders; EQ out; Weiss latency.
    {
        auto s = base (false);
        s.msEq = true;
        s.bands[1].gainDb = 4.0;
        s.sideBands[2].gainDb = -3.0;
        Engine a, b;
        a.prepare (fs, 512, 2);
        b.prepare (fs, 512, 2);
        const auto first = run (a, s, x);
        s.compFirst = true;
        const auto second = run (b, s, x);
        check (maxAbsDiff (first[0], second[0]) == 0.0, "M/S works in both EQ/compressor orders");

        Engine c;
        c.prepare (fs, 512, 2);
        s.compFirst = false;
        s.eqOn = false;
        const auto out = run (c, s, x);
        check (maxAbsDiff (out[0], x[0]) < 1e-7 && maxAbsDiff (out[1], x[1]) < 1e-7, "with the EQ out in M/S, the input passes unchanged (encode and decode are transparent)");

        Engine w;
        w.prepare (fs, 512, 2);
        auto t = base (true);
        t.msEq = true;
        const auto y = run (w, t, x);
        double d = 0.0;
        for (size_t i = (size_t) weissLatencySamples + 64; i < y[0].size(); ++i)
            d = std::max (d, std::abs ((double) y[0][i] - (double) x[0][i - (size_t) weissLatencySamples]));
        check (w.latencySamples() == weissLatencySamples && d < 2e-6, "Weiss in M/S: flat is an exact delay of " + juce::String (weissLatencySamples) + " samples (worst " + fmt (20.0 * std::log10 (d + 1e-12), 0) + " dBFS)");
    }

    // A mono track stays on the plain path.
    {
        Engine a, b;
        a.prepare (fs, 512, 1);
        b.prepare (fs, 512, 1);
        auto s = base (false);
        s.bands[1].gainDb = 4.0;
        s.sideBands[1].gainDb = 9.0;
        auto ya = x[0], yb = x[0];
        MeterData m;
        for (int start = 0; start + 256 <= (int) ya.size(); start += 256)
        {
            float* pa[1] = { ya.data() + start };
            float* pb[1] = { yb.data() + start };
            auto on = s;
            on.msEq = true;
            a.process (pa, 1, 256, on, m);
            b.process (pb, 1, 256, s, m);
        }
        check (maxAbsDiff (ya, yb) == 0.0, "on a mono track the M/S switch changes nothing");
    }

    // Switching Stereo and M/S is click-free, with the EQ in use.
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto s = base (false);
        s.bands[1] = { Shape::bell, 1000.0, 6.0, 1.0 };
        const auto tone = sine ((int) fs / 2, fs, 200.0, 0.25f);
        const auto before = run (e, s, tone);
        s.msEq = true;
        auto y = run (e, s, tone);
        s.msEq = false;
        auto z = run (e, s, tone);
        double step = 0.0;
        for (const auto* v : { &y[0], &z[0] })
            for (size_t i = 1; i < v->size(); ++i)
                step = std::max (step, std::abs ((double) (*v)[i] - (double) (*v)[i - 1]));
        const double clean = 2.0 * 0.25 * std::sin (juce::MathConstants<double>::pi * 200.0 / fs);
        check (step < 1.5 * clean, "switching Stereo and M/S doesn't click (largest sample step " + fmt (step, 4) + ", the tone's own " + fmt (clean, 4) + ")");
        check (std::abs (rmsDb (y[0], (size_t) (0.05 * fs)) - rmsDb (before[0], (size_t) (0.05 * fs))) < 0.1, "after the switch to M/S the level is back within 50 ms");
    }

    // Parameters: reach the engine, keep independent values, recall, and old sets.
    {
        CoherenceProcessor p;
        auto& st = p.state();
        const auto set = [&] (const juce::String& pid, float v) { auto* q = st.getParameter (pid); jassert (q != nullptr); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        const auto val = [&] (const juce::String& pid) { return st.getRawParameterValue (pid)->load(); };
        ParameterReader reader (st);
        reader.attachAppended (st);
        const auto def = reader.read();
        check (! def.msEq && std::abs (def.sideBands[0].gainDb) < 1e-5 && std::abs (def.sideBands[3].frequency - 10000.0) < 1.0, "a new instance is in stereo, with the Side channel flat");

        set (id::eqMs, 1.0f);
        set ("lf_gain", 2.0f);
        set ("side_lf_gain", -5.0f);
        set ("side_hmf_freq", 3150.0f);
        set ("side_hf_type", 2.0f);
        const auto r = reader.read();
        check (r.msEq && std::abs (r.bands[0].gainDb - 2.0) < 1e-3 && std::abs (r.sideBands[0].gainDb + 5.0) < 1e-3 && std::abs (r.sideBands[2].frequency - 3150.0) < 1.0 && r.sideBands[3].shape == Shape::highCut,
               "the Side parameters reach the Side EQ, and the main ones the Mid EQ");

        set (id::eqChannel, 1.0f);
        const auto selected = reader.read();
        check (std::abs (selected.bands[0].gainDb - 2.0) < 1e-3 && std::abs (selected.sideBands[0].gainDb + 5.0) < 1e-3, "switching the Mid/Side selector changes no EQ setting");

        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        CoherenceProcessor back;
        back.setStateInformation (saved.getData(), (int) saved.getSize());
        const auto v = [&] (const juce::String& pid) { return back.state().getRawParameterValue (pid)->load(); };
        check (v (id::eqMs) >= 0.5f && v (id::eqChannel) >= 0.5f && std::abs (v ("side_lf_gain") + 5.0f) < 1e-3f && std::abs (v ("lf_gain") - 2.0f) < 1e-3f, "M/S, the selector and both channels' settings are saved and recalled");

        p.selectAbSlot (1);   // starts as a copy of A; then it gets its own settings
        set (id::eqMs, 0.0f);
        set ("side_lf_gain", 0.0f);
        p.selectAbSlot (0);
        const bool slotA = val (id::eqMs) >= 0.5f && std::abs (val ("side_lf_gain") + 5.0f) < 1e-3f;
        p.selectAbSlot (1);
        const bool slotB = val (id::eqMs) < 0.5f && std::abs (val ("side_lf_gain")) < 1e-3f;
        p.selectAbSlot (0);
        check (slotA && slotB && val (id::eqMs) >= 0.5f && std::abs (val ("side_lf_gain") + 5.0f) < 1e-3f, "A/B carries M/S and the Side settings");

        // A set from before M/S: stereo and a flat Side, whatever this instance held.
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        std::vector<juce::XmlElement*> gone;
        for (auto* c : xml->getChildWithTagNameIterator ("PARAM"))
            if (const auto pid = c->getStringAttribute ("id"); pid.startsWith ("side_") || pid == id::eqMs || pid == id::eqChannel)
                gone.push_back (c);
        for (auto* c : gone)
            xml->removeChildElement (c, true);
        if (auto* other = xml->getChildByName ("OTHER_SLOT"))
            other->deleteAllChildElements();
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        CoherenceProcessor over;
        over.state().getParameter (id::eqMs)->setValueNotifyingHost (1.0f);
        auto* q = over.state().getParameter ("side_lf_gain");
        q->setValueNotifyingHost (q->convertTo0to1 (6.0f));
        over.setStateInformation (old.getData(), (int) old.getSize());
        const auto u = [&] (const juce::String& pid) { return over.state().getRawParameterValue (pid)->load(); };
        check (u (id::eqMs) < 0.5f && std::abs (u ("side_lf_gain")) < 1e-3f && std::abs (u ("lf_gain") - 2.0f) < 1e-3f, "a set from before M/S loads in stereo with a flat Side, even over an instance in M/S");

        int meter = -1, firstNew = -1, index = 0, count = 0;
        for (auto* pp : p.getParameters())
        {
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (pp))
            {
                const auto pid = rp->getParameterID();
                if (isMeterParameter (*rp)) meter = index;
                if (firstNew < 0 && (pid == id::eqOutput || pid == id::eqMs || pid.startsWith ("side_lf"))) firstNew = index;
                if (pid.startsWith ("side_")) ++count;
            }
            ++index;
        }
        check (count == 14 && firstNew > meter, "14 Side parameters, all after the meter, so that no existing index moves");
    }
}

void msPanelChecks()
{
    CoherenceProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* ed = dynamic_cast<CoherenceEditor*> (editor.get());
    auto& st = p.state();
    const auto set = [&] (const juce::String& pid, float v) { auto* q = st.getParameter (pid); q->setValueNotifyingHost (q->convertTo0to1 (v)); ed->tickMetersForPreview (1); };
    const auto val = [&] (const juce::String& pid) { return st.getRawParameterValue (pid)->load(); };
    const auto shows = [&] (const char* pid) { auto* k = ed->findKnob (pid); return k != nullptr && k->isVisible(); };
    const auto allBands = [&] (bool side)
    {
        for (const auto& b : id::bands)
        {
            const juce::String key = side ? id::sideBand (b) : b;
            for (const auto& pid : { id::bandGain (key), id::bandFreq (key), id::bandQ (key) })
                if (auto* k = ed->findKnob (pid); k == nullptr || ! k->isVisible())
                    return false;
        }
        return true;
    };

    ed->tickMetersForPreview (1);
    check (allBands (false) && ! shows ("side_lf_gain") && ! ed->findEqChannelSelector()->isEnabled(), "panel, stereo: the main EQ controls show, and the Mid/Side selector rests");

    set (id::eqMs, 1.0f);
    check (allBands (false) && ! shows ("side_lf_gain") && ed->findEqChannelSelector()->isEnabled(), "panel, M/S on MID: the main EQ controls show, and the selector works");

    set ("lf_gain", 2.0f);
    set (id::eqChannel, 1.0f);
    check (allBands (true) && ! shows ("lf_gain"), "panel, M/S on SIDE: the Side controls show in the same places");

    ed->findKnob ("side_lf_gain")->stepBy (10, true);
    check (std::abs (val ("side_lf_gain") - 0.1f) < 1e-3f && std::abs (val ("lf_gain") - 2.0f) < 1e-3f, "a control on SIDE moves only the Side setting");

    set (id::eqChannel, 0.0f);
    check (allBands (false) && std::abs (val ("side_lf_gain") - 0.1f) < 1e-3f && std::abs (val ("lf_gain") - 2.0f) < 1e-3f, "back on MID, both channels' settings are as they were");

    set (id::eqChannel, 1.0f);
    set (id::eqMs, 0.0f);
    check (allBands (false) && ! shows ("side_lf_gain") && std::abs (val (id::eqChannel) - 1.0f) < 1e-3f, "leaving M/S shows the main EQ again and keeps the selector for the next time");

    // Side LF type: its own controls follow its shape, the main EQ's don't.
    set (id::eqMs, 1.0f);
    set ("side_lf_type", 2.0f);
    auto* sideGain = ed->findKnob ("side_lf_gain");
    auto* mainGain = ed->findKnob ("lf_gain");
    check (sideGain != nullptr && mainGain != nullptr && val ("lf_type") < 1.5f, "the Side band shapes are independent of the main ones");
}

void eqFocusChecks()
{
    line ("\n## EQ focus parameters (the UC1 follows the Mid/Side selector)\n");
    CoherenceProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    auto& st = p.state();
    juce::SharedResourcePointer<UserSettings> userSettings;
    const auto savedStep = userSettings->gainStepping();
    userSettings->setGainStepping (GainStepping::fine);

    const auto focusOf = [&] (const juce::String& pid) -> juce::AudioParameterFloat*
    {
        for (auto* q : p.getParameters())
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (q); f != nullptr && f->getParameterID() == juce::String (id::focusPrefix) + pid)
                return f;
        return nullptr;
    };
    // What a host does when a surface moves a control: the value, then the listeners, on its own thread.
    const auto hostSet = [] (juce::RangedAudioParameter& q, float v)
    {
        auto& base = static_cast<juce::AudioProcessorParameter&> (q);
        const float n = q.convertTo0to1 (v);
        base.setValue (n);
        base.sendValueChangedMessageToListeners (n);
    };
    const auto val = [&] (const juce::String& pid) { return st.getRawParameterValue (pid)->load(); };

    {
        // auval fails when a parameter moves another one without declaring it.
        int meta = 0;
        for (auto* q : p.getParameters())
            if (auto* f = dynamic_cast<juce::RangedAudioParameter*> (q); f != nullptr && f->getParameterID().startsWith (id::focusPrefix) && f->isMetaParameter())
                ++meta;
        check (meta == 12, "the 12 focus parameters are declared as moving other parameters (AU meta flag)");
    }
    const auto panelSet = [&] (const juce::String& pid, float v)
    {
        const ScopedWriteSource gui (WriteSource::gui);
        auto* q = st.getParameter (pid);
        q->setValueNotifyingHost (q->convertTo0to1 (v));
    };
    const auto plain = [] (juce::RangedAudioParameter& q) { return q.convertFrom0to1 (q.getValue()); };

    // The set of parameters.
    {
        int count = 0, firstFocus = -1, last = -1, automatable = 0, index = 0;
        for (auto* q : p.getParameters())
        {
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q); r != nullptr && r->getParameterID().startsWith (id::focusPrefix))
            {
                ++count;
                if (firstFocus < 0) firstFocus = index;
                if (q->isAutomatable()) ++automatable;
            }
            last = index++;
        }
        check (count == 12 && firstFocus == last - 12 && automatable == 0, "12 focus parameters, after every setting so that no existing index moves (only the A/B switch comes after them), none automatable");
    }

    auto& fGain = *focusOf ("lf_gain");
    auto& fFreq = *focusOf ("hmf_freq");
    auto& fQ = *focusOf ("hf_q");

    // Stereo: the main EQ.
    hostSet (fGain, 2.5f);
    check (std::abs (val ("lf_gain") - 2.5f) < 1e-3f && std::abs (val ("side_lf_gain")) < 1e-3f, "in stereo a surface's move reaches the main EQ");

    // M/S on Side: only Side. Frequency and Q too.
    juce::Thread::sleep (160);
    panelSet (id::eqMs, 1.0f);
    panelSet (id::eqChannel, 1.0f);
    p.focus().sync();
    check (std::abs (plain (fGain)) < 1e-3f, "selecting SIDE brings the surface's control to the Side value (" + fmt (plain (fGain), 2) + " dB)");
    juce::Thread::sleep (2);
    hostSet (fGain, -4.0f);
    hostSet (fFreq, 3150.0f);
    hostSet (fQ, 2.0f);
    check (std::abs (val ("side_lf_gain") + 4.0f) < 1e-3f && std::abs (val ("lf_gain") - 2.5f) < 1e-3f, "on SIDE a surface's move reaches only the Side channel");
    check (std::abs (val ("side_hmf_freq") - 3150.0f) < 1.0f && std::abs (val ("hmf_freq") - 2500.0f) < 1.0f && std::abs (val ("side_hf_q") - 2.0f) < 0.02f && std::abs (val ("hf_q") - 0.707f) < 0.01f,
           "frequency and Q follow the selector too");

    // Back on Mid: the surface reads Mid again, and nothing was reset.
    juce::Thread::sleep (160);
    panelSet (id::eqChannel, 0.0f);
    p.focus().sync();
    check (std::abs (plain (fGain) - 2.5f) < 1e-3f && std::abs (plain (fFreq) - 2500.0f) < 1.0f && std::abs (val ("side_lf_gain") + 4.0f) < 1e-3f, "selecting MID brings the surface back to Mid's values, and Side keeps its own");

    // Panel to surface.
    juce::Thread::sleep (2);
    panelSet ("lf_gain", 1.0f);
    panelSet ("side_lf_gain", -9.0f);
    p.focus().sync();
    check (std::abs (plain (fGain) - 1.0f) < 1e-3f, "a panel move on the selected channel reaches the surface; one on the other channel doesn't");

    // The old mapping (a surface bound to lf_gain itself) still works, in stereo and in Mid.
    juce::Thread::sleep (2);
    hostSet (*st.getParameter ("lf_gain"), 0.5f);
    p.focus().sync();
    check (std::abs (val ("lf_gain") - 0.5f) < 1e-3f && std::abs (plain (fGain) - 0.5f) < 1e-3f, "a surface bound to the band's own parameter still works, and the focus follows it");

    // A VST3 host passes sync()'s writes back later. That must not undo a move made since.
    {
        panelSet ("lf_gain", 3.0f);
        p.focus().sync();                      // tells the host 3.0
        panelSet ("lf_gain", 4.0f);            // the panel moves on
        hostSet (fGain, 3.0f);                 // the host's echo of 3.0 arrives
        check (std::abs (val ("lf_gain") - 4.0f) < 1e-3f, "a host's late echo of the focus value doesn't move the channel back");
        p.focus().sync();
        check (std::abs (plain (fGain) - 4.0f) < 1e-3f, "the focus then settles on the channel's value");

        // After the echo window, the same value is a real move.
        juce::Thread::sleep (320);
        hostSet (fGain, 3.0f);
        check (std::abs (val ("lf_gain") - 3.0f) < 1e-3f, "later, a surface returning to that value is a real move");
    }

    // While the surface is moving, sync() doesn't fight it, even if the channel is a block behind.
    {
        juce::Thread::sleep (160);
        hostSet (fGain, 6.0f);
        hostSet (*st.getParameter ("lf_gain"), 5.9f);   // the host's lagging echo of the earlier value
        p.focus().sync();
        check (std::abs (plain (fGain) - 6.0f) < 1e-3f, "sync() leaves a control alone while the surface is moving it");
        hostSet (fGain, 6.0f);
    }

    // Gain stepping: a surface's move through the focus plays and reads on the grid, on the selected channel.
    {
        juce::Thread::sleep (160);
        userSettings->setGainStepping (GainStepping::standard);
        panelSet (id::eqChannel, 1.0f);
        p.focus().sync();
        juce::Thread::sleep (2);
        hostSet (fGain, 2.23f);
        ParameterReader reader (st);
        reader.attachAppended (st);
        const auto s = reader.read();
        const auto text = fGain.getCurrentValueAsText();
        auto* sideGain = dynamic_cast<SteppedGainParameter*> (st.getParameter ("side_lf_gain"));
        check (sideGain != nullptr && sideGain->isExternal() && std::abs (s.sideBands[0].gainDb - 2.0) < 1e-3 && text.contains ("2.0"),
               "with stepping at 0.5 dB, a surface's 2.23 dB plays at " + fmt (s.sideBands[0].gainDb, 2) + " dB and reads " + text);
        check (std::abs (val ("side_lf_gain") - 2.23f) < 1e-3f, "the stored value stays what the surface sent");
        userSettings->setGainStepping (savedStep);
    }

    // Not saved, not in A/B; after recall and A/B the surface reads the channel.
    {
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        const auto xmlText = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize())->toString();
        check (! xmlText.contains (id::focusPrefix), "the focus parameters aren't saved with the set");

        CoherenceProcessor back;
        back.setStateInformation (mb.getData(), (int) mb.getSize());
        back.focus().sync();
        juce::RangedAudioParameter* backFocus = nullptr;
        for (auto* q : back.getParameters())
            if (auto* f = dynamic_cast<juce::RangedAudioParameter*> (q); f != nullptr && f->getParameterID() == "focus_lf_gain")
                backFocus = f;
        check (backFocus != nullptr && std::abs (plain (*backFocus) - 2.23f) < 1e-3f, "after recall the surface reads the selected channel");

        juce::Thread::sleep (160);
        p.selectAbSlot (1);   // starts as a copy of A; then it gets its own value
        p.focus().sync();
        check (std::abs (plain (fGain) - 2.23f) < 1e-3f, "the first switch moves nothing, so the surface reads the same value");
        juce::Thread::sleep (160);
        hostSet (fGain, 0.0f);
        juce::Thread::sleep (160);
        p.selectAbSlot (0);
        p.focus().sync();
        juce::Thread::sleep (160);
        p.selectAbSlot (1);
        p.focus().sync();
        check (std::abs (plain (fGain)) < 1e-3f, "after A/B the surface reads the other slot's value");
        p.selectAbSlot (0);
        p.focus().sync();
        check (std::abs (plain (fGain) - 2.23f) < 1e-3f, "and back");
    }
}

void processorState()
{
    line ("\n## Plug-in state, A/B, automation\n");
    CoherenceProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    auto& s = p.state();
    auto set = [&] (const char* pid, float v) { auto* q = s.getParameter (pid); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    set (id::threshold, -31.5f);
    set ("hmf_freq", 4321.0f);
    set (id::order, 1.0f);
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    CoherenceProcessor p2;
    p2.setStateInformation (mb.getData(), (int) mb.getSize());
    const float t = p2.state().getRawParameterValue (id::threshold)->load();
    const float f = p2.state().getRawParameterValue ("hmf_freq")->load();
    check (std::abs (t + 31.5f) < 0.01f && std::abs (f - 4321.0f) < 1.0f && p2.state().getRawParameterValue (id::order)->load() > 0.5f,
           "state save / restore round trip");

    // A set saved by the v0.4.x development builds names the SSL 292 parameters, which this release doesn't have.
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        for (auto* c : xml->getChildIterator())
            if (c->getStringAttribute ("id") == id::eqMode)
                c->setAttribute ("value", 0.0);   // those builds' default
        for (auto* pid : { "sat_model", "sat_drive", "auto_gain" })
        {
            auto* c = xml->createNewChildElement ("PARAM");
            c->setAttribute ("id", pid);
            c->setAttribute ("value", 1.0);
        }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        CoherenceProcessor o;
        o.setPlayConfigDetails (2, 2, 48000.0, 512);
        o.setStateInformation (old.getData(), (int) old.getSize());
        o.prepareToPlay (48000.0, 512);
        juce::MemoryBlock resaved;
        o.getStateInformation (resaved);
        const auto text = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize())->toString();
        check (std::abs (o.state().getRawParameterValue (id::threshold)->load() + 31.5f) < 0.01f && o.getLatencySamples() == 0 && ! text.contains ("sat_") && ! text.contains ("auto_gain"),
               "a set from an earlier build loads: saturation and global AUTO GAIN settings ignored, no latency, not saved again");
    }

    // AUTO makeup: on in a new instance; a saved set keeps its own setting.
    {
        CoherenceProcessor fresh;
        const bool isOn = fresh.state().getRawParameterValue (id::autoMakeup)->load() >= 0.5f;
        auto* q = fresh.state().getParameter (id::autoMakeup);
        q->setValueNotifyingHost (0.0f);
        juce::MemoryBlock saved;
        fresh.getStateInformation (saved);
        CoherenceProcessor loaded;
        loaded.setStateInformation (saved.getData(), (int) saved.getSize());
        check (isOn && loaded.state().getRawParameterValue (id::autoMakeup)->load() < 0.5f, "AUTO makeup is on in a new instance, and a set saved with it off stays off");
    }

    // Every parameter at a random value survives save/restore (what a DAW does on recall).
    {
        std::mt19937 r2 (11);
        std::uniform_real_distribution<float> u2 (0.0f, 1.0f);
        for (auto* param : p.getParameters())
            param->setValueNotifyingHost (u2 (r2));
        juce::MemoryBlock mb2;
        p.getStateInformation (mb2);
        CoherenceProcessor p3;
        p3.setStateInformation (mb2.getData(), (int) mb2.getSize());
        int diffs = 0;
        juce::String first;
        auto plain = [] (juce::AudioProcessorParameter* q)
        {
            // compare what the parameter means: bools by state, everything else by its (snapped) value
            if (auto* b = dynamic_cast<juce::AudioParameterBool*> (q)) return b->get() ? 1.0f : 0.0f;
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q)) return r->convertFrom0to1 (q->getValue());
            return q->getValue();
        };
        int saved = 0;
        for (int i = 0; i < p.getParameters().size(); ++i)
            if (auto* savedParam = dynamic_cast<juce::RangedAudioParameter*> (p.getParameters()[i]); savedParam != nullptr && isSavedParameter (*savedParam) && ++saved > 0
                && std::abs (plain (p.getParameters()[i]) - plain (p3.getParameters()[i])) > 1e-5f * std::max (1.0f, std::abs (plain (p.getParameters()[i]))))
                if (diffs++ == 0) first = p.getParameters()[i]->getName (64) + " " + juce::String (p.getParameters()[i]->getValue()) + " vs " + juce::String (p3.getParameters()[i]->getValue());
        check (diffs == 0, "all " + juce::String (saved) + " parameters at random values survive save/restore (" + juce::String (diffs) + " differences " + first
                               + "; the read-only gain-reduction meter is not part of the set)");
        set (id::threshold, -31.5f); set ("hmf_freq", 4321.0f); set (id::order, 1.0f); set (id::bypass, 0.0f);
    }

    p.copyCurrentToOtherSlot();
    set (id::threshold, -10.0f);
    p.selectAbSlot (1);
    check (std::abs (s.getRawParameterValue (id::threshold)->load() + 31.5f) < 0.01f, "A/B: switching slots restores the other settings");
    p.selectAbSlot (0);
    check (std::abs (s.getRawParameterValue (id::threshold)->load() + 10.0f) < 0.01f, "A/B: and back");

    // Automation every block for every float parameter: output stays finite and bounded.
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    bool finite = true;
    float peak = 0;
    for (int b = 0; b < 2000; ++b)
    {
        for (auto* param : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
                if (rp->getParameterID() != id::bypass && rp->getParameterID() != id::output && rp->getParameterID() != id::input
                    && ! rp->getParameterID().endsWith ("_gain") && rp->getParameterID() != id::makeup && rp->getParameterID() != id::side && rp->getParameterID() != id::mid)
                    rp->setValueNotifyingHost (u (rng));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buf.setSample (c, i, 0.25f * std::sin ((float) (b * 512 + i) * 0.05f));
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                finite = finite && std::isfinite (buf.getSample (c, i));
                peak = std::max (peak, std::abs (buf.getSample (c, i)));
            }
    }
    check (finite && peak < 4.0f, "random automation of every parameter, every block: finite, bounded output (peak " + fmt (peak, 3) + ")");
}

//==============================================================================
// v0.2.1 measurement additions: phase, sample-rate consistency, slopes, bands against analog, latency,
// distortion, null tests, host behaviour (block sizes, sample-rate changes, several instances).

constexpr double kPi = juce::MathConstants<double>::pi;
const double allRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

// Impulse response of the engine (left channel), trimmed where it has decayed below 1e-13 for good.
std::vector<double> engineIR (const Settings& s, double fs, double seconds)
{
    Engine e;
    e.prepare (fs, 512, 2);
    const int n = (int) (fs * seconds);
    std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n, 0.0f));
    x[0][0] = x[1][0] = 1.0f;
    auto y = run (e, s, x);
    std::vector<double> h (y[0].begin(), y[0].end());
    size_t last = h.size();
    while (last > 1 && std::abs (h[last - 1]) < 1e-13) --last;
    h.resize (last);
    return h;
}

// Complex frequency response of an impulse response (rotating phasor, renormalised every 1024 samples).
std::complex<double> irResponse (const std::vector<double>& h, double f, double fs)
{
    const double w = -2.0 * kPi * f / fs;
    const auto rot = std::polar (1.0, w);
    std::complex<double> z = 1.0, acc = 0.0;
    for (size_t n = 0; n < h.size(); ++n)
    {
        acc += h[n] * z;
        z = ((n & 1023) == 1023) ? std::polar (1.0, w * (double) (n + 1)) : z * rot;
    }
    return acc;
}

// The analog prototype of a band as set on the panel (bells: panel Q -> RBJ design Q, as the engine does).
std::complex<double> panelAnalog (const BandSettings& b, double f)
{
    double q = effectiveQ (b.shape, b.q);
    if (b.shape == Shape::bell)
        q = bellDesignQ (q, b.gainDb);
    return analogResponse (b.shape, b.frequency, b.gainDb, q, f);
}

int bandSlotFor (Shape s) { return (s == Shape::lowShelf || s == Shape::lowCut) ? 0 : ((s == Shape::highShelf || s == Shape::highCut) ? 3 : 2); }

double wrapDeg (std::complex<double> ratio) { return std::arg (ratio) * 180.0 / kPi; }

struct NamedBand { BandSettings b; const char* name; };
const NamedBand phaseSet[] = {
    { { Shape::bell, 100.0, 6.0, 1.0 }, "Bell 100 Hz Q1 +6" },
    { { Shape::bell, 1000.0, 12.0, 1.0 }, "Bell 1 kHz Q1 +12" },
    { { Shape::bell, 1000.0, -6.0, 4.0 }, "Bell 1 kHz Q4 -6" },
    { { Shape::bell, 5000.0, 6.0, 0.7 }, "Bell 5 kHz Q0.7 +6" },
    { { Shape::bell, 10000.0, 6.0, 1.0 }, "Bell 10 kHz Q1 +6" },
    { { Shape::bell, 15000.0, -9.0, 2.0 }, "Bell 15 kHz Q2 -9" },
    { { Shape::highShelf, 3000.0, 6.0, 0.707 }, "High shelf 3 kHz +6" },
    { { Shape::highShelf, 10000.0, -6.0, 0.707 }, "High shelf 10 kHz -6" },
    { { Shape::lowShelf, 100.0, 6.0, 0.707 }, "Low shelf 100 Hz +6" },
    { { Shape::lowCut, 40.0, 0.0, 0.707 }, "Low cut 40 Hz Q0.71" },
    { { Shape::lowCut, 100.0, 0.0, 2.0 }, "Low cut 100 Hz Q2" },
    { { Shape::highCut, 8000.0, 0.0, 0.707 }, "High cut 8 kHz Q0.71" },
    { { Shape::highCut, 15000.0, 0.0, 0.707 }, "High cut 15 kHz Q0.71" },
};

// Phase against analog, and the same setting across sample rates (magnitude and phase), from one set of
// measured impulse responses.
void eqPhaseAndRates()
{
    const auto freqs = logFreqs (20.0, 20000.0, 121);   // 1/12 octave
    const int numRates = (int) std::size (allRates);
    // resp[setting][rate][freq]
    std::vector<std::vector<std::vector<std::complex<double>>>> resp;
    for (const auto& nb : phaseSet)
    {
        auto s = neutral();
        s.bands[(size_t) bandSlotFor (nb.b.shape)] = nb.b;
        std::vector<std::vector<std::complex<double>>> perRate;
        for (double fs : allRates)
        {
            const auto h = engineIR (s, fs, nb.b.frequency < 300.0 ? 1.5 : 0.4);
            std::vector<std::complex<double>> r;
            for (double f : freqs) r.push_back (irResponse (h, f, fs));
            perRate.push_back (r);
        }
        resp.push_back (perRate);
    }

    line ("\n## EQ: phase response against the analog prototype\n");
    line ("Phase of the running filter (from its measured impulse response) minus the phase of the analog prototype,");
    line ("one band active, in degrees: worst case from 20 Hz to 10 kHz / from 10 to 20 kHz.");
    line ("Bells and shelves are minimum phase, like the analog filters. Their remaining phase difference is inherent to a");
    line ("second-order section at that sample rate: a fit to the complex (magnitude and phase) response does not reduce it");
    line ("(Tools/phase_fit_experiment.py), and it falls about fourfold per doubling of the rate. The high cut is a separate,");
    line ("understood limitation (DESIGN.md section 8).\n");
    juce::String hdr = "| Setting |", sep = "|---|";
    for (double fs : allRates) { hdr << " " << fmt (fs / 1000.0, 1) << " kHz |"; sep << "---|"; }
    line (hdr);
    line (sep);
    double worstLo4448 = 0, worstLoHigh = 0, worstHiHigh = 0, hcLo = 0, hcLo96 = 0;
    for (size_t k = 0; k < std::size (phaseSet); ++k)
    {
        const bool highCut = phaseSet[k].b.shape == Shape::highCut;
        juce::String row = juce::String ("| ") + phaseSet[k].name + " |";
        for (int r = 0; r < numRates; ++r)
        {
            double lo = 0, hi = 0;
            for (size_t i = 0; i < freqs.size(); ++i)
            {
                const auto a = panelAnalog (phaseSet[k].b, freqs[i]);
                if (std::abs (a) < 1e-3) continue;   // below -60 dB (cut stopbands): phase not meaningful
                const double d = std::abs (wrapDeg (resp[k][(size_t) r][i] / a));
                (freqs[i] <= 10000.0 ? lo : hi) = std::max (freqs[i] <= 10000.0 ? lo : hi, d);
            }
            if (highCut) { hcLo = std::max (hcLo, lo); if (allRates[r] == 96000.0) hcLo96 = std::max (hcLo96, lo); }
            else if (allRates[r] < 50000.0) worstLo4448 = std::max (worstLo4448, lo);
            else { worstLoHigh = std::max (worstLoHigh, lo); worstHiHigh = std::max (worstHiHigh, hi); }
            row << " " << fmt (lo, 2) << " / " << fmt (hi, 1) << " |";
        }
        line (row);
    }
    line ("");
    // Regression limits set from the v0.2.1 measurement with margin (not specifications).
    check (worstLo4448 < 7.0, "bells, shelves, low cut at 44.1/48 kHz: phase within 7 degrees of analog up to 10 kHz (worst " + fmt (worstLo4448, 2) + " degrees)");
    check (worstLoHigh < 3.5 && worstHiHigh < 8.0, "... at 88.2-192 kHz: within 3.5 degrees up to 10 kHz and 8 degrees up to 20 kHz (worst "
                                                      + fmt (worstLoHigh, 2) + " / " + fmt (worstHiHigh, 2) + " degrees)");
    limitation ("high cut: the magnitude follows analog but the phase leads it by up to " + juce::String (juce::roundToInt (hcLo)) + " degrees below 10 kHz ("
                + juce::String (juce::roundToInt (hcLo96)) + " at 96 kHz). The matched design responds from the first sample, about half a sample ahead of"
                " the analog filter. A fix is proposed in DESIGN.md section 8, pending approval because it changes the sound.");

    line ("\n## EQ: consistency across sample rates\n");
    line ("The same settings at 44.1, 48, 88.2, 96, 176.4 and 192 kHz, measured from the running filter. Spread = the largest");
    line ("difference between any two sample rates at the same frequency.\n");
    line ("| Setting | Magnitude spread 20 Hz-10 kHz | 10-16 kHz | 16-20 kHz | Phase spread 20 Hz-10 kHz | 10-20 kHz |");
    line ("|---|---|---|---|---|---|");
    double worstSpread10 = 0, worstSpread16 = 0, worstPhase10 = 0;
    for (size_t k = 0; k < std::size (phaseSet); ++k)
    {
        const bool highCut = phaseSet[k].b.shape == Shape::highCut;
        double m10 = 0, m16 = 0, m20 = 0, p10 = 0, p20 = 0;
        for (size_t i = 0; i < freqs.size(); ++i)
        {
            double mlo = 1e9, mhi = -1e9, plo = 1e9, phi = -1e9;
            const auto a = panelAnalog (phaseSet[k].b, freqs[i]);
            for (int r = 0; r < numRates; ++r)
            {
                const auto z = resp[k][(size_t) r][i];
                const double m = 20.0 * std::log10 (std::abs (z) + 1e-300), p = wrapDeg (z / a);
                mlo = std::min (mlo, m); mhi = std::max (mhi, m); plo = std::min (plo, p); phi = std::max (phi, p);
            }
            const double ms = mhi - mlo, ps = std::abs (a) < 1e-3 ? 0.0 : phi - plo;
            if (freqs[i] <= 10000.0) { m10 = std::max (m10, ms); p10 = std::max (p10, ps); }
            else { p20 = std::max (p20, ps); (freqs[i] <= 16000.0 ? m16 : m20) = std::max (freqs[i] <= 16000.0 ? m16 : m20, ms); }
        }
        worstSpread10 = std::max (worstSpread10, m10);
        worstSpread16 = std::max (worstSpread16, m16);
        if (! highCut) worstPhase10 = std::max (worstPhase10, p10);
        line (juce::String ("| ") + phaseSet[k].name + " | " + fmt (m10, 3) + " dB | " + fmt (m16, 3) + " dB | " + fmt (m20, 2) + " dB | "
              + fmt (p10, 2) + " deg | " + fmt (p20, 1) + " deg |");
    }
    line ("");
    // Regression limits from the v0.2.1 measurement (the spread is the 44.1 kHz fit's top-octave residual).
    check (worstSpread10 < 0.15, "the same settings give the same magnitude at every sample rate up to 10 kHz, within 0.15 dB (worst spread " + fmt (worstSpread10, 3) + " dB)");
    check (worstSpread16 < 0.5, "... and within 0.5 dB from 10 to 16 kHz (worst spread " + fmt (worstSpread16, 3) + " dB)");
    check (worstPhase10 < 7.0, "bells, shelves, low cut: phase within 7 degrees across sample rates up to 10 kHz (worst " + fmt (worstPhase10, 2) + " degrees)");
}

void eqSlopes()
{
    line ("\n## EQ: cut filter slopes\n");
    line ("Measured slope in the stopband (dB per octave) of the running filter against the analog prototype.");
    line ("Low cut 100 Hz: between 12.5 and 25 Hz. High cut 2 kHz: between 8 and 16 kHz.\n");
    line ("| Filter | Analog | 44.1 kHz | 48 kHz | 88.2 kHz | 96 kHz | 192 kHz |");
    line ("|---|---|---|---|---|---|---|");
    double worst = 0;
    for (auto [shape, f0, q, fa, fb, name] : { std::tuple { Shape::lowCut, 100.0, 0.707, 25.0, 12.5, "Low cut 100 Hz Q0.71" },
                                               std::tuple { Shape::lowCut, 100.0, 2.0, 25.0, 12.5, "Low cut 100 Hz Q2" },
                                               std::tuple { Shape::highCut, 2000.0, 0.707, 8000.0, 16000.0, "High cut 2 kHz Q0.71" },
                                               std::tuple { Shape::highCut, 2000.0, 2.0, 8000.0, 16000.0, "High cut 2 kHz Q2" } })
    {
        const BandSettings b { shape, f0, 0.0, q };
        const double an = 20.0 * std::log10 (std::abs (panelAnalog (b, fa)) / std::abs (panelAnalog (b, fb)));
        juce::String row = juce::String ("| ") + name + " | " + fmt (an, 2) + " |";
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto s = neutral();
            s.bands[(size_t) bandSlotFor (shape)] = b;
            const auto h = engineIR (s, fs, 1.0);
            const double m = 20.0 * std::log10 (std::abs (irResponse (h, fa, fs)) / std::abs (irResponse (h, fb, fs)));
            worst = std::max (worst, std::abs (m - an));
            row << " " << fmt (m, 2) << " |";
        }
        line (row);
    }
    line ("");
    check (worst < 0.5, "cut filter slopes within 0.5 dB/octave of analog at every rate (worst " + fmt (worst, 2) + ")");
}

// Four bands together against the product of the four analog prototypes.
void eqBandsAgainstAnalog()
{
    line ("\n## EQ: four bands together against analog\n");
    line ("2,000 random four-band settings per sample rate (LF: shelf, bell or cut; LMF, HMF: bells; HF: shelf, bell or cut;");
    line ("gains +/-12 dB, the panel's frequency and Q ranges), cascade against the product of the analog prototypes.");
    line ("Computed from the designs; the running cascade equals its designs to within 1e-4 dB (\"several bands together\").\n");
    line ("| Sample rate | Max error 20 Hz-10 kHz | 99th pct | Median | Max error 10-16 kHz | Max error 16-20 kHz | Worst case up to 10 kHz |");
    line ("|---|---|---|---|---|---|---|");
    std::mt19937 rng (77);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    const auto freqs = logFreqs (20.0, 20000.0, 97);
    double worst10_4448 = 0, p99_4448 = 0;
    auto shapeName = [] (Shape s) { return s == Shape::bell ? "bell" : (s == Shape::lowShelf || s == Shape::highShelf) ? "shelf" : "cut"; };
    for (double fs : allRates)
    {
        std::vector<double> e10s;
        double e16 = 0, e20 = 0, worstE = 0;
        juce::String worstAt;
        for (int t = 0; t < 2000; ++t)
        {
            auto pick = [&] (double lo, double hi) { return lo * std::pow (hi / lo, u (rng)); };
            BandSettings b[4];
            const double r0 = u (rng), r3 = u (rng);
            b[0] = { r0 < 0.4 ? Shape::lowShelf : (r0 < 0.8 ? Shape::bell : Shape::lowCut), pick (16, 1000), -12 + 24 * u (rng), pick (0.3, 10) };
            b[1] = { Shape::bell, pick (40, 4000), -12 + 24 * u (rng), pick (0.3, 10) };
            b[2] = { Shape::bell, pick (300, 16000), -12 + 24 * u (rng), pick (0.3, 10) };
            b[3] = { r3 < 0.4 ? Shape::highShelf : (r3 < 0.8 ? Shape::bell : Shape::highCut), pick (1000, 20000), -12 + 24 * u (rng), pick (0.3, 10) };
            double e10 = 0, at = 0;
            for (double f : freqs)
            {
                if (f > 0.4535 * fs) continue;
                double dig = 0, ana = 0;
                for (const auto& bb : b)
                {
                    dig += biquadMagnitudeDb (engineDesign (bb.shape, bb.frequency, bb.gainDb, bb.q, fs), f, fs);
                    ana += 20.0 * std::log10 (std::abs (panelAnalog (bb, f)) + 1e-300);
                }
                if (ana < -24.0) continue;   // inside a cut's stopband
                const double e = std::abs (dig - ana);
                if (f <= 10000.0) { if (e > e10) { e10 = e; at = f; } } else if (f <= 16000.0) e16 = std::max (e16, e); else e20 = std::max (e20, e);
            }
            e10s.push_back (e10);
            if (e10 > worstE)
            {
                worstE = e10;
                worstAt = fmt (at, 0) + " Hz; band with the largest own error there: ";
                double big = -1;
                for (const auto& bb : b)
                {
                    const double own = std::abs (biquadMagnitudeDb (engineDesign (bb.shape, bb.frequency, bb.gainDb, bb.q, fs), at, fs)
                                                 - 20.0 * std::log10 (std::abs (panelAnalog (bb, at)) + 1e-300));
                    if (own > big) { big = own; worstAt = fmt (at, 0) + " Hz, from a " + shapeName (bb.shape) + " at " + fmt (bb.frequency, 0) + " Hz, Q "
                                                          + fmt (effectiveQ (bb.shape, bb.q), 2) + ", " + fmt (bb.gainDb, 1) + " dB (" + fmt (own, 2) + " dB of it)"; }
                }
            }
        }
        std::sort (e10s.begin(), e10s.end());
        const double p99 = e10s[e10s.size() * 99 / 100];
        if (fs < 50000.0) { worst10_4448 = std::max (worst10_4448, e10s.back()); p99_4448 = std::max (p99_4448, p99); }
        line ("| " + fmt (fs / 1000.0, 1) + " kHz | " + fmt (e10s.back(), 3) + " dB | " + fmt (p99, 3) + " dB | " + fmt (e10s[e10s.size() / 2], 4) + " dB | "
              + fmt (e16, 3) + " dB | " + fmt (e20, 2) + " dB | " + worstAt + " |");
    }
    line ("");
    check (p99_4448 < 0.3, "four random bands together: 99% of settings within 0.3 dB of analog up to 10 kHz at 44.1/48 kHz (worst p99 " + fmt (p99_4448, 3) + " dB)");
    check (worst10_4448 < 1.0, "... and every setting within 1 dB (worst " + fmt (worst10_4448, 3) + " dB; the largest errors come from bands set near 20 kHz, see the worst case)");
}

void latency()
{
    line ("\n## Latency\n");
    bool zero = true;
    for (double fs : allRates)
    {
        CoherenceProcessor p;
        p.state().getParameter (id::eqMode)->setValueNotifyingHost (0.0f);
        p.setPlayConfigDetails (2, 2, fs, 512);
        p.prepareToPlay (fs, 512);
        zero = zero && p.getLatencySamples() == 0;
    }
    check (zero, "in COHERENCE mode the plug-in reports 0 samples of latency at every sample rate");

    // Measured: an impulse at sample 1000 with every section active.
    const double fs = 48000.0;
    Engine e;
    e.prepare (fs, 512, 2);
    auto s = neutral();
    s.bands[0] = { Shape::lowShelf, 120.0, 4.0, 0.707 };
    s.bands[1] = { Shape::bell, 400.0, -3.0, 1.0 };
    s.bands[2] = { Shape::bell, 3000.0, 5.0, 1.4 };
    s.bands[3] = { Shape::highShelf, 9000.0, 3.0, 0.707 };
    s.dynOn = true;
    s.comp = { -30.0, 4.0, 5.0, 100.0, false, 0.3, 3.0, false, 1.0, 0.0 };
    s.midDb = -0.5; s.sideDb = 1.5;
    const int n = 24000;
    std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n, 0.0f));
    x[0][1000] = x[1][1000] = 0.25f;
    auto y = run (e, s, x);
    int first = -1, peak = 0;
    for (int i = 0; i < n; ++i)
    {
        if (first < 0 && y[0][(size_t) i] != 0.0f) first = i;
        if (std::abs (y[0][(size_t) i]) > std::abs (y[0][(size_t) peak])) peak = i;
    }
    check (first == 1000 && peak == 1000, "an impulse comes out at the sample it went in, with every section active (first output sample "
                                              + juce::String (first - 1000) + ", peak at " + juce::String (peak - 1000) + " samples); no look-ahead");
    line ("The EQ is minimum phase like the analog filters it follows: frequency-dependent group delay equal to the analog");
    line ("prototype's (the phase table above), and no pure delay.");
}

// THD+N of a steady sine: residual after a least-squares fit of the fundamental (and DC), relative to it.
double thdnDb (const std::vector<float>& y, double f, double fs, size_t from)
{
    double sss = 0, scc = 0, ssc = 0, sy = 0, cy = 0, s1 = 0, c1 = 0, y1 = 0;
    const size_t n = y.size() - from;
    for (size_t i = from; i < y.size(); ++i)
    {
        const double ph = 2.0 * kPi * f * (double) i / fs, s = std::sin (ph), c = std::cos (ph), v = y[i];
        sss += s * s; scc += c * c; ssc += s * c; sy += s * v; cy += c * v; s1 += s; c1 += c; y1 += v;
    }
    // Solve the 3x3 normal equations for a*sin + b*cos + d.
    const double N = (double) n;
    const double M[3][3] = { { sss, ssc, s1 }, { ssc, scc, c1 }, { s1, c1, N } };
    const double r[3] = { sy, cy, y1 };
    auto det3 = [] (const double m[3][3]) { return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]); };
    const double D = det3 (M);
    double sol[3];
    for (int k = 0; k < 3; ++k)
    {
        double T[3][3];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) T[i][j] = j == k ? r[i] : M[i][j];
        sol[k] = det3 (T) / D;
    }
    double res = 0, fund = 0;
    for (size_t i = from; i < y.size(); ++i)
    {
        const double ph = 2.0 * kPi * f * (double) i / fs, fit = sol[0] * std::sin (ph) + sol[1] * std::cos (ph);
        res += std::pow (y[i] - fit - sol[2], 2.0);
        fund += fit * fit;
    }
    return 10.0 * std::log10 (res / fund + 1e-300);
}

// Amplitude of harmonic k of f (least-squares sin/cos over a whole number of cycles).
double harmonicAmp (const std::vector<float>& y, double f, double fs, size_t from, size_t len)
{
    double a = 0, b = 0;
    for (size_t i = from; i < from + len; ++i)
    {
        const double ph = 2.0 * kPi * f * (double) i / fs;
        a += y[i] * std::sin (ph);
        b += y[i] * std::cos (ph);
    }
    return 2.0 * std::sqrt (a * a + b * b) / (double) len;
}

void distortion()
{
    line ("\n## Distortion\n");
    // EQ: linear filtering in double precision. THD+N is set by the 32-bit float input and output.
    {
        double worst = -1000;
        juce::String detail;
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            const double fs = 48000.0;
            Engine e;
            e.prepare (fs, 512, 2);
            auto s = neutral();
            s.bands[0] = { Shape::lowShelf, 120.0, 6.0, 0.707 };
            s.bands[1] = { Shape::bell, 400.0, -4.0, 1.0 };
            s.bands[2] = { Shape::bell, 3000.0, 6.0, 1.0 };
            s.bands[3] = { Shape::highShelf, 10000.0, 3.0, 0.707 };
            auto x = sine ((int) fs * 2, fs, f, 0.25f);
            auto y = run (e, s, x);
            const double d = thdnDb (y[0], f, fs, (size_t) fs);
            const double din = thdnDb (x[0], f, fs, (size_t) fs);
            worst = std::max (worst, d);
            detail << (detail.isEmpty() ? "" : ", ") << fmt (f, 0) << " Hz " << fmt (d, 1) << " dB (input itself " << fmt (din, 1) << " dB)";
        }
        line ("EQ, four bands active (+6 dB shelf, -4 dB bell, +6 dB bell, +3 dB shelf), -12 dBFS sine, THD+N: " + detail + ".");
        check (worst < -130.0, "the EQ adds no distortion: THD+N at the level of the 32-bit float signal itself (worst " + fmt (worst, 1) + " dB)");
    }

    // Compressor: harmonic distortion from gain modulation (the detector follows the waveform at short times).
    line ("\nCompressor, threshold -20 dB, 4:1, knee 0, sine peak at -10 dBFS (about 6 dB of gain reduction), steady state.");
    line ("THD (harmonics 2-9) and the strongest harmonic, against Pro-C 3 Clean measured at the same settings. Like Pro-C 3,");
    line ("the detector reads the instantaneous peak, so on low tones with short times the gain follows the waveform a little.\n");
    double worstVsRef = -1000.0;
    line ("| Attack / release | 50 Hz | 100 Hz | 1 kHz |");
    line ("|---|---|---|---|");
    // Pro-C 3 Clean THD (dB) at 50 / 100 / 1000 Hz (Tools/proc3/compare.py, section 2).
    for (auto [att, rel, autoRel, name, r50, r100, r1k] : { std::tuple { 1.0, 50.0, false, "1 ms / 50 ms", -32.3, -38.2, -58.1 },
                                                            std::tuple { 10.0, 150.0, false, "10 ms / 150 ms (default)", -38.8, -44.8, -64.7 },
                                                            std::tuple { 30.0, 300.0, false, "30 ms / 300 ms", -44.1, -50.1, -70.0 },
                                                            std::tuple { 10.0, 150.0, true, "10 ms / AUTO", 0.0, 0.0, 0.0 } })
    {
        juce::String row = juce::String ("| ") + name + " |";
        const double ref[3] = { r50, r100, r1k };
        int fi = 0;
        for (double f : { 50.0, 100.0, 1000.0 })
        {
            const double fs = 48000.0;
            Engine e;
            e.prepare (fs, 512, 2);
            auto s = neutral();
            s.dynOn = true;
            s.comp = { -20.0, 4.0, att, rel, autoRel, 0.0, 0.0, false, 1.0, 0.0 };
            auto x = sine ((int) fs * 4, fs, f, (float) std::pow (10.0, -10.0 / 20.0));
            auto y = run (e, s, x);
            const size_t from = (size_t) fs * 2, len = (size_t) fs * 2;   // whole number of cycles for 50/100/1000 Hz
            const double h1 = harmonicAmp (y[0], f, fs, from, len);
            double hs = 0, best = 0;
            int bestK = 2;
            for (int k = 2; k <= 9; ++k)
            {
                const double hk = harmonicAmp (y[0], f * k, fs, from, len);
                hs += hk * hk;
                if (hk > best) { best = hk; bestK = k; }
            }
            const double thd = 10.0 * std::log10 (hs / (h1 * h1) + 1e-300);
            row << " " << fmt (thd, 1) << " dB (" << fmt (100.0 * std::sqrt (hs) / h1, 3) << " %), H" << bestK;
            if (ref[fi] < 0.0)
            {
                row << ", Pro-C 3 " << fmt (ref[fi], 1) << " dB";
                worstVsRef = std::max (worstVsRef, thd - ref[fi]);
            }
            row << " |";
            ++fi;
        }
        line (row);
    }
    // The Pro-C 3 figures were measured with an FFT (Hann window, Tools/proc3/compare.py), these with the suite's
    // harmonic fit: the methods differ by about 1.5 dB on the same file (in compare.py COHERENCE reads 0.4-0.9 dB below Pro-C 3).
    check (worstVsRef < 1.5, "compressor THD on steady 50 Hz-1 kHz tones is no higher than Pro-C 3 Clean's at the same settings (+1.5 dB method tolerance; worst " + fmt (worstVsRef, 2) + " dB)");
}

void nullTests()
{
    line ("\n## Null tests and host behaviour\n");
    const double fs = 48000.0;
    auto busy = neutral();
    busy.bands[0] = { Shape::lowShelf, 90.0, 3.0, 0.707 };
    busy.bands[1] = { Shape::bell, 350.0, -2.5, 1.2 };
    busy.bands[2] = { Shape::bell, 2800.0, 4.0, 0.9 };
    busy.bands[3] = { Shape::highShelf, 11000.0, 2.0, 0.707 };
    busy.dynOn = true;
    busy.comp = { -28.0, 3.0, 8.0, 160.0, false, 0.4, 2.0, false, 1.0, 80.0 };
    busy.midDb = 0.3; busy.sideDb = -1.2; busy.balance = 0.1;
    auto x = noise ((int) fs * 2, 0.15f, 21);

    auto exact = [] (const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b)
    { return std::max (maxAbsDiff (a[0], b[0]), maxAbsDiff (a[1], b[1])); };

    {   // Bypass
        auto s = busy; s.bypass = true;
        Engine e; e.prepare (fs, 512, 2);
        const double d = exact (x, run (e, s, x));
        check (d == 0.0, "BYPASS (power off) nulls completely against the input (max difference " + juce::String (d, 12) + ")");
    }
    {   // Compressor in, signal below threshold and knee
        auto s = neutral(); s.dynOn = true;
        s.comp = { -6.0, 4.0, 10.0, 150.0, false, 0.4, 0.0, false, 1.0, 0.0 };
        auto xq = noise ((int) fs, 0.01f, 4);
        Engine e; e.prepare (fs, 512, 2);
        const double d = exact (xq, run (e, s, xq));
        check (d == 0.0, "compressor IN below threshold nulls completely (max difference " + juce::String (d, 12) + ")");
    }
    {   // DYN out: the order switch changes nothing
        auto a = busy; a.dynOn = false; a.compFirst = false;
        auto b = a; b.compFirst = true;
        Engine e1, e2; e1.prepare (fs, 512, 2); e2.prepare (fs, 512, 2);
        const double d = exact (run (e1, a, x), run (e2, b, x));
        check (d == 0.0, "with DYN out, EQ > DYN and DYN > EQ null completely (max difference " + juce::String (d, 12) + ")");
    }
    {   // Determinism
        Engine e1, e2; e1.prepare (fs, 512, 2); e2.prepare (fs, 512, 2);
        const double d = exact (run (e1, busy, x), run (e2, busy, x));
        check (d == 0.0, "two instances with the same settings and input null completely (deterministic)");
    }
    {   // Host block size: 32, 64, 512, 1024 and random sizes give the same output
        Engine ref; ref.prepare (fs, 4096, 2);
        const auto yRef = run (ref, busy, x, 512);
        double worst = 0;
        for (int blk : { 32, 64, 128, 1024, 4096 })
        {
            Engine e; e.prepare (fs, 4096, 2);
            worst = std::max (worst, exact (yRef, run (e, busy, x, blk)));
        }
        {
            Engine e; e.prepare (fs, 4096, 2);
            MeterData m;
            auto y = x;
            std::mt19937 rng (9);
            for (int start = 0; start < (int) x[0].size();)
            {
                const int len = std::min (1 + (int) (rng() % 700), (int) x[0].size() - start);
                float* p[2] = { y[0].data() + start, y[1].data() + start };
                e.process (p, 2, len, busy, m);
                start += len;
            }
            worst = std::max (worst, exact (yRef, y));
        }
        check (worst < 1e-6, "host buffer size does not change the sound: 32 to 4096 samples and random sizes 1-700 (max difference "
                                 + juce::String (worst, 9) + ")");
    }
    {   // Sample-rate change: re-preparing an instance gives exactly a fresh instance
        Engine e; e.prepare (48000.0, 512, 2);
        run (e, busy, x);
        e.prepare (96000.0, 512, 2);
        auto x96 = noise (96000, 0.15f, 22);
        const auto y1 = run (e, busy, x96);
        Engine f; f.prepare (96000.0, 512, 2);
        const double d = exact (y1, run (f, busy, x96));
        e.prepare (44100.0, 512, 2);
        auto x44 = noise (44100, 0.15f, 23);
        const auto y2 = run (e, busy, x44);
        Engine g; g.prepare (44100.0, 512, 2);
        const double d2 = exact (y2, run (g, busy, x44));
        check (d == 0.0 && d2 == 0.0, "changing the sample rate (48 -> 96 -> 44.1 kHz) leaves no state behind: output equals a fresh instance");
    }
    {   // Several instances processed interleaved do not affect each other
        auto other = busy;
        other.bands[2] = { Shape::bell, 700.0, -8.0, 3.0 };
        other.comp.thresholdDb = -40.0;
        Engine a, b, a1, b1;
        for (auto* e : { &a, &b, &a1, &b1 }) e->prepare (fs, 512, 2);
        MeterData ma, mb;
        auto ya = x, yb = x;
        for (int start = 0; start < (int) x[0].size(); start += 256)
        {
            const int len = std::min (256, (int) x[0].size() - start);
            float* pa[2] = { ya[0].data() + start, ya[1].data() + start };
            float* pb[2] = { yb[0].data() + start, yb[1].data() + start };
            a.process (pa, 2, len, busy, ma);
            b.process (pb, 2, len, other, mb);
        }
        const double d = std::max (exact (ya, run (a1, busy, x)), exact (yb, run (b1, other, x)));
        check (d == 0.0, "two instances with different settings, processed interleaved, each equal a lone instance (no shared state)");
    }
    {   // The plug-in wrapper adds nothing: processor output equals the engine with the settings it reads
        CoherenceProcessor p;
        p.setPlayConfigDetails (2, 2, fs, 512);
        p.prepareToPlay (fs, 512);
        auto& st = p.state();
        auto set = [&] (const juce::String& pid, float v) { auto* q = st.getParameter (pid); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        set ("hmf_gain", 4.5f); set ("hmf_freq", 2200.0f); set ("lf_gain", 2.0f); set (id::side, 1.5f); set (id::threshold, -30.0f);
        set (id::dynOn, 1.0f);
        ParameterReader reader (st);
        reader.attachAppended (st);
        const auto settings = reader.read();
        check (settings.eqOutputDb == 0.0, "EQ Output's default is exactly 0 dB (read " + juce::String (settings.eqOutputDb, 12) + "), so it can't change a sample");
        Engine e; e.prepare (fs, 512, 2);
        const auto yE = run (e, settings, x, 512);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        auto yP = x;
        for (int start = 0; start + 512 <= (int) x[0].size(); start += 512)
        {
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, x[(size_t) c].data() + start, 512);
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + 512, yP[(size_t) c].data() + start);
        }
        const size_t whole = (x[0].size() / 512) * 512;
        double d = 0;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < whole; ++i) d = std::max (d, (double) std::abs (yP[(size_t) c][i] - yE[(size_t) c][i]));
        check (d == 0.0, "the plug-in's output nulls completely against the engine run with the settings it reads (max difference " + juce::String (d, 12) + ")");
    }
}

// The remaining controls: SC HPF, manual makeup, infinite ratio, AUTO release, DYN IN switching.
void compressorControls()
{
    const double fs = 48000.0;
    auto settle = [&] (const CompSettings& c, double f, double peakDb, bool on = true)
    {
        Engine e; e.prepare (fs, 512, 2);
        auto s = neutral(); s.dynOn = on; s.comp = c;
        auto x = sine ((int) fs * 2, fs, f, (float) std::pow (10.0, peakDb / 20.0));
        auto y = run (e, s, x);
        return rmsDb (y[0], (size_t) fs) - rmsDb (x[0], (size_t) fs);
    };
    const double noHpf = settle ({ -20.0, 4.0, 10.0, 150.0, false, 0.0, 0.0, false, 1.0, 0.0 }, 50.0, -10.0);
    const double withHpf = settle ({ -20.0, 4.0, 10.0, 150.0, false, 0.0, 0.0, false, 1.0, 200.0 }, 50.0, -10.0);
    check (std::abs (noHpf + 6.09) < 0.15 && withHpf > -0.3, "SC HPF: a 50 Hz tone peaking 10 dB over threshold is reduced " + fmt (noHpf, 2) + " dB (Pro-C 3: 6.09), and "
                                                                + fmt (withHpf, 2) + " dB with the sidechain high-pass at 200 Hz");
    const double makeup = settle ({ -20.0, 4.0, 5.0, 150.0, false, 0.0, 3.0, false, 1.0, 0.0 }, 1000.0, -30.0);
    const double inf = [&]
    {
        Engine e; e.prepare (fs, 512, 2);
        auto s = neutral(); s.dynOn = true; s.comp = { -20.0, 1000.0, 5.0, 150.0, false, 0.0, 0.0, false, 1.0, 0.0 };
        auto x = squareWave ((int) fs * 2, fs, 1000.0, (float) std::pow (10.0, -10.0 / 20.0));
        auto y = run (e, s, x);
        return rmsDb (y[0], (size_t) fs) - rmsDb (x[0], (size_t) fs);
    }();
    check (std::abs (makeup - 3.0) < 0.01 && std::abs (inf + 10.0) < 0.02, "MAKEUP +3 dB adds " + fmt (makeup, 3) + " dB; ratio infinity holds a signal 10 dB over threshold at threshold ("
                                                                             + fmt (inf, 2) + " dB)");

    // AUTO release against Pro-C 3 Clean's Auto Release (DC step 10 dB over, 4:1, attack 1 ms, release knob 100 ms):
    // recovery to 37 % after 105.5 ms following a 10 ms burst and 297 ms following 2 s of reduction.
    auto recovery = [&] (double burstSeconds)
    {
        Engine e; e.prepare (fs, 512, 2);
        auto s = neutral(); s.dynOn = true;
        s.comp = { -20.0, 4.0, 1.0, 100.0, true, 0.0, 0.0, false, 1.0, 0.0 };
        const int pre = (int) (0.2 * fs), on = (int) (burstSeconds * fs), n = pre + on + (int) fs * 3;
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n, 0.01f));
        for (int i = pre; i < pre + on; ++i)
            x[0][(size_t) i] = x[1][(size_t) i] = 0.316228f;
        auto y = run (e, s, x);
        auto gr = [&] (int i) { return -20.0 * std::log10 ((double) y[0][(size_t) i] / (double) x[0][(size_t) i]); };
        const double end = gr (pre + on - 1);
        for (int i = pre + on; i < n; ++i)
            if (gr (i) <= 0.368 * end) return (i - pre - on) / fs * 1000.0;
        return -1.0;
    };
    const double fast = recovery (0.01), slow = recovery (2.0);
    check (std::abs (fast - 105.5) < 0.15 * 105.5 && std::abs (slow - 297.0) < 0.15 * 297.0,
           "AUTO release: recovery to 37 % " + fmt (fast, 0) + " ms after a 10 ms burst (Pro-C 3: 106), " + fmt (slow, 0) + " ms after 2 s of reduction (Pro-C 3: 297)");

    // DYN IN switched off and on while compressing: the gain moves smoothly (no click).
    {
        Engine e; e.prepare (fs, 512, 2);
        MeterData m;
        auto s = neutral(); s.comp = { -20.0, 4.0, 5.0, 150.0, false, 0.0, 0.0, false, 1.0, 0.0 };
        const int n = (int) fs * 2;
        auto x = sine (n, fs, 1000.0, (float) std::pow (10.0, -10.0 / 20.0));
        auto y = x;
        for (int start = 0, blk = 0; start < n; start += 512, ++blk)
        {
            s.dynOn = blk < 60 || blk >= 120;
            float* ptr[2] = { y[0].data() + start, y[1].data() + start };
            e.process (ptr, 2, std::min (512, n - start), s, m);
        }
        double worstStep = 0, prev = 0;
        for (int c = 24000; c < n - 48; c += 48)   // 1 ms windows from 0.5 s
        {
            double a = 0, b = 0;
            for (int i = c - 24; i < c + 24; ++i) { a = std::max (a, (double) std::abs (x[0][(size_t) i])); b = std::max (b, (double) std::abs (y[0][(size_t) i])); }
            const double gdb = 20.0 * std::log10 (b / a);
            if (c > 24000) worstStep = std::max (worstStep, std::abs (gdb - prev));
            prev = gdb;
        }
        // A click would put the whole 7.5 dB change into one window; the switch fades over about 7 ms.
        check (worstStep < 3.0, "DYN IN off and on while reducing 7.5 dB: no step, the change is spread over several ms (largest " + fmt (worstStep, 2) + " dB per ms)");
    }
}

void compressorLink()
{
    // Stereo link: a loud left channel and a quiet right channel get identical gain reduction.
    const double fs = 48000.0;
    Engine e;
    e.prepare (fs, 512, 2);
    auto s = neutral();
    s.dynOn = true;
    s.comp = { -20.0, 4.0, 5.0, 150.0, false, 0.0, 0.0, false, 1.0, 0.0 };
    auto x = sine ((int) fs * 2, fs, 1000.0, 0.5f, 0.01f);
    auto y = run (e, s, x);
    const double gL = rmsDb (y[0], (size_t) fs) - rmsDb (x[0], (size_t) fs), gR = rmsDb (y[1], (size_t) fs) - rmsDb (x[1], (size_t) fs);
    check (std::abs (gL - gR) < 1e-6 && gL < -3.0, "stereo link: left at -6 dBFS and right at -40 dBFS get the same gain reduction (L " + fmt (gL, 3)
                                                        + " dB, R " + fmt (gR, 3) + " dB), so the image does not shift");
}

//==============================================================================
// Host edge cases (v0.2.2 audit): mono tracks, oversized and empty blocks, recall without a B slot, A/B switching
// while the host saves, tail length.
void hostEdgeCases()
{
    line ("\n## Host edge cases\n");
    const double fs = 48000.0;
    auto setParam = [] (CoherenceProcessor& p, const juce::String& pid, float v)
    { auto* q = p.state().getParameter (pid); q->setValueNotifyingHost (q->convertTo0to1 (v)); };

    {   // Mono track: MID is a level control, SIDE and BALANCE have no effect, nothing breaks.
        CoherenceProcessor p;
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        const bool ok = p.setBusesLayout (mono);
        p.setPlayConfigDetails (1, 1, fs, 512);
        p.prepareToPlay (fs, 512);
        setParam (p, id::mid, -6.0f);
        setParam (p, id::side, 9.0f);
        setParam (p, id::balance, 50.0f);
        juce::AudioBuffer<float> buf (1, 512);
        juce::MidiBuffer midi;
        double inE = 0, outE = 0;
        bool finite = true;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < 512; ++i) buf.setSample (0, i, 0.25f * (float) std::sin (2.0 * kPi * 1000.0 * (b * 512 + i) / fs));
            if (b >= 100) for (int i = 0; i < 512; ++i) inE += std::pow (buf.getSample (0, i), 2.0);
            p.processBlock (buf, midi);
            if (b >= 100) for (int i = 0; i < 512; ++i) { outE += std::pow (buf.getSample (0, i), 2.0); finite = finite && std::isfinite (buf.getSample (0, i)); }
        }
        const double g = 10.0 * std::log10 (outE / inE);
        check (ok && finite && std::abs (g + 6.0) < 0.01, "mono track: the layout is accepted, MID -6 dB lowers the level by " + fmt (g, 3)
                                                        + " dB, SIDE and BALANCE have no effect");
    }
    {   // Oversized block (host sends more than prepared) and an empty block.
        CoherenceProcessor a, b;
        for (auto* p : { &a, &b }) { p->setPlayConfigDetails (2, 2, fs, 512); p->prepareToPlay (fs, 512); setParam (*p, "hmf_gain", 6.0f); setParam (*p, id::dynOn, 1.0f); }
        auto x = noise (8192, 0.2f, 31);
        juce::AudioBuffer<float> big (2, 8192), small (2, 512), empty (2, 0);
        juce::MidiBuffer midi;
        for (int c = 0; c < 2; ++c) big.copyFrom (c, 0, x[(size_t) c].data(), 8192);
        a.processBlock (empty, midi);
        a.processBlock (big, midi);
        double d = 0;
        for (int start = 0; start < 8192; start += 512)
        {
            for (int c = 0; c < 2; ++c) small.copyFrom (c, 0, x[(size_t) c].data() + start, 512);
            b.processBlock (small, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) d = std::max (d, (double) std::abs (small.getSample (c, i) - big.getSample (c, start + i)));
        }
        check (d == 0.0, "a block 16 times larger than prepared, and an empty block, are handled: same output as 512-sample blocks (max difference " + juce::String (d, 12) + ")");
    }
    {   // Recall of a state without a second A/B slot: B starts as a copy of A.
        CoherenceProcessor p;
        setParam (p, "hmf_gain", 4.0f);
        p.copyCurrentToOtherSlot();                       // B = +4 dB
        setParam (p, "hmf_gain", -2.0f);                  // A = -2 dB
        auto plain = p.state().copyState();               // a state with no OTHER_SLOT child
        juce::MemoryBlock mb;
        if (auto xml = plain.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        setParam (p, "hmf_gain", 7.0f);
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        const float a = p.state().getParameter ("hmf_gain")->convertFrom0to1 (p.state().getParameter ("hmf_gain")->getValue());
        p.selectAbSlot (1);
        const float bv = p.state().getParameter ("hmf_gain")->convertFrom0to1 (p.state().getParameter ("hmf_gain")->getValue());
        check (std::abs (a + 2.0f) < 1e-4f && std::abs (bv + 2.0f) < 1e-4f, "recalling a state without an A/B slot: A is restored (" + fmt (a, 2)
                                                                           + " dB) and B starts as a copy of it (" + fmt (bv, 2) + " dB), not the B from before");
    }
    {   // A/B switching on one thread while the host saves state on another: no crash, every saved state readable.
        CoherenceProcessor p;
        setParam (p, "lf_gain", 3.0f);
        p.copyCurrentToOtherSlot();
        std::atomic<bool> stop { false };
        std::atomic<int> saves { 0 }, bad { 0 };
        std::thread saver ([&]
        {
            while (! stop.load())
            {
                juce::MemoryBlock mb;
                p.getStateInformation (mb);
                if (juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()) == nullptr) ++bad;
                ++saves;
            }
        });
        for (int i = 0; i < 400; ++i) { p.selectAbSlot (i & 1); setParam (p, "lf_gain", (float) (i % 7)); }
        stop = true;
        saver.join();
        check (bad.load() == 0 && saves.load() > 0, "A/B switching while the host saves state on another thread: " + juce::String (saves.load())
                                                         + " saves, all readable");
    }
    {   // A state saved by v0.2.1 (apvts.copyState() layout, window size in the tree, second slot attached) loads.
        CoherenceProcessor old;
        setParam (old, "hmf_gain", 3.5f);
        setParam (old, id::mid, -1.2f);
        auto tree = old.state().copyState();
        tree.setProperty ("uiScale", 1.25f, nullptr);
        tree.setProperty ("abSlot", 0, nullptr);
        juce::ValueTree other ("OTHER_SLOT");
        juce::ValueTree slot ("COHERENCE");
        juce::ValueTree prm ("PARAM"); prm.setProperty ("id", "hmf_gain", nullptr); prm.setProperty ("value", -4.0f, nullptr);
        slot.appendChild (prm, nullptr);
        other.appendChild (slot, nullptr);
        tree.appendChild (other, nullptr);
        juce::MemoryBlock mb;
        if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        CoherenceProcessor p;
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        auto val = [&] (const juce::String& pid) { auto* q = p.state().getParameter (pid); return q->convertFrom0to1 (q->getValue()); };
        const float g = val ("hmf_gain"), m = val (id::mid), sc = p.getUiScale();
        p.selectAbSlot (1);
        const float gB = val ("hmf_gain");
        check (std::abs (g - 3.5f) < 1e-4f && std::abs (m + 1.2f) < 1e-4f && std::abs (sc - 1.25f) < 1e-6f && std::abs (gB + 4.0f) < 1e-4f,
               "a v0.2.1 session loads: parameters, window size (" + fmt (sc, 2) + ") and the B slot are restored");
    }
    {
        CoherenceProcessor p;
        check (p.getTailLengthSeconds() >= 1.6, "tail length reported to the host covers the longest filter ring-out (" + fmt (p.getTailLengthSeconds(), 1) + " s)");
    }
}

//==============================================================================
// v0.2.2: gain stepping (SETUP menu) and window size.
void gainSteppingAndWindow()
{
    line ("\n## Gain stepping and window size\n");
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("coherence-settings-test");
    dir.deleteRecursively();
    dir.createDirectory();
    UserSettings::setFileForTesting (dir.getChildFile ("COHERENCE.settings"));

    {   // Persistence and fallback.
        {
            juce::SharedResourcePointer<UserSettings> s;
            check (s->gainStepping() == GainStepping::fine, "a new user starts in Fine (0.1 dB), the v0.2 behaviour");
            s->setGainStepping (GainStepping::standard);
        }
        {
            juce::SharedResourcePointer<UserSettings> s;   // a fresh object reads the file again
            check (s->gainStepping() == GainStepping::standard, "the gain stepping choice persists between sessions (read back from the settings file)");
            s->setGainStepping (GainStepping::fine);
        }
        dir.getChildFile ("COHERENCE.settings").replaceWithText ("<?xml version=\"1.0\"?><PROPERTIES><VALUE name=\"gainStepping\" val=\"7\"/></PROPERTIES>");
        juce::SharedResourcePointer<UserSettings> s;
        check (s->gainStepping() == GainStepping::fine, "an unknown stored value falls back to Fine");
    }

    juce::SharedResourcePointer<UserSettings> settings;   // held for the rest of the test (one shared instance)
    const juce::StringArray dbIds { "lf_gain", "lmf_gain", "hmf_gain", "hf_gain", id::input, id::mid, id::side, id::threshold, id::makeup, id::eqOutput, "side_lf_gain", "side_lmf_gain", "side_hmf_gain", "side_hf_gain" };

    CoherenceProcessor p, p2;
    std::unique_ptr<juce::AudioProcessorEditor> e1 (p.createEditor()), e2 (p2.createEditor());
    auto* ed = dynamic_cast<CoherenceEditor*> (e1.get());
    auto* ed2 = dynamic_cast<CoherenceEditor*> (e2.get());
    auto val = [] (juce::RangedAudioParameter& q) { return q.convertFrom0to1 (q.getValue()); };

    bool allOk = true;
    juce::String detail;
    for (auto mode : { GainStepping::fine, GainStepping::standard, GainStepping::precision })
    {
        settings->setGainStepping (mode);
        settings->dispatchPendingMessages();   // deliver the change message now
        const float step = gainStepDb (mode);
        for (const auto& pid : dbIds)
        {
            auto* k = ed->findKnob (pid);
            if (k == nullptr) { allOk = false; detail << pid << " not found; "; continue; }
            auto& q = k->parameter();
            q.setValueNotifyingHost (q.convertTo0to1 (pid == id::threshold ? -20.0f : 0.0f));
            const float v0 = val (q);
            k->stepBy (1, false);
            const float v1 = val (q);
            k->stepBy (-3, false);
            const float v2 = val (q);
            k->stepBy (1, true);   // Shift
            const float v3 = val (q);
            // Drag: an arbitrary position snaps onto the step grid.
            k->dragTo (0.6137f, false);
            const float vd = val (q);
            const float onGrid = std::abs (vd / step - std::round (vd / step));
            const bool ok = std::abs ((v1 - v0) - step) < 1e-4f && std::abs ((v2 - v1) + 3 * step) < 1e-4f
                            && std::abs ((v3 - v2) - 0.01f) < 1e-4f && onGrid < 1e-3f && k->getCoarseStep() == step;
            if (! ok) { allOk = false; detail << pid << " in " << gainSteppingLabel (mode) << "; "; }
        }
        // The second editor (another instance) follows the change.
        if (auto* k2 = ed2->findKnob (id::mid); k2 == nullptr || k2->getCoarseStep() != step) { allOk = false; detail << "second instance did not follow; "; }
        // The output fader.
        auto* f = ed->findFader();
        auto& out = *p.state().getParameter (id::output);
        out.setValueNotifyingHost (out.convertTo0to1 (0.0f));
        f->stepBy (2, false);
        const float fo = val (out);
        f->stepBy (-1, true);
        const float fo2 = val (out);
        if (std::abs (fo - 2 * step) > 1e-4f || std::abs (fo2 - (fo - 0.01f)) > 1e-4f) { allOk = false; detail << "fader in " << gainSteppingLabel (mode) << "; "; }
    }
    check (allOk, "every dB control (4 EQ gains, IN TRIM, MID, SIDE, THRESHOLD, MAKEUP, OUTPUT) steps by exactly 0.1 / 0.5 / 0.01 dB in Fine / Standard / Precision, "
                  "drags land on the step grid, Shift gives 0.01 dB, and a second open instance follows the change" + (detail.isEmpty() ? juce::String() : " (" + detail + ")"));

    {   // Typed values stay exact in every mode; a host value is stored as sent and played on the grid.
        settings->setGainStepping (GainStepping::standard);
        settings->dispatchPendingMessages();
        auto& q = *p.state().getParameter (id::mid);
        auto* sq = dynamic_cast<SteppedGainParameter*> (&q);
        {
            const ScopedWriteSource gui (WriteSource::gui);   // the knob's text entry writes from the GUI
            q.setValueNotifyingHost (q.getValueForText ("1.37"));
        }
        const float typed = val (q), typedPlays = sq->effective (typed);
        q.setValueNotifyingHost (q.convertTo0to1 (0.03f));   // host automation / controller value off the step grid
        const float host = val (q), hostPlays = sq->effective (host);
        check (std::abs (typed - 1.37f) < 1e-5f && typedPlays == typed && std::abs (host - 0.03f) < 1e-5f && hostPlays == 0.0f,
               "in Standard (0.5 dB) a typed 1.37 dB stays and plays 1.37 dB; a host or controller value of 0.03 dB is stored as sent (0.03) and plays 0.0 dB");
        settings->setGainStepping (GainStepping::fine);
        settings->dispatchPendingMessages();
    }

    {   // Window size: the panel scales as a whole, the size is kept with the set, presets and limits.
        bool ok = true;
        juce::String sizes;
        for (float sc : { 0.6f, 0.8f, 1.0f, 1.2f, 1.4f, 2.0f })
        {
            ed->setScale (sc);
            const float expectW = (float) CoherenceEditor::baseWidth * sc;
            ok = ok && std::abs ((float) ed->getWidth() - expectW) <= 1.0f && std::abs (ed->getScale() - sc) < 0.002f
                 && std::abs (p.getUiScale() - ed->getScale()) < 1e-6f;
            // Every control scales with the panel: the MID knob's on-screen bounds are its base bounds times the scale.
            auto* k = ed->findKnob (id::mid);
            const auto onScreen = ed->getLocalArea (k, k->getLocalBounds()).toFloat();
            const auto base = k->getBoundsInParent().toFloat();
            const float toScreen = ed->getScale() * CoherenceEditor::panelToBase;
            // Within 2: at a fractional scale, the area on screen is rounded outwards to whole pixels.
            ok = ok && std::abs (onScreen.getWidth() - base.getWidth() * toScreen) < 2.0f && std::abs (onScreen.getX() - base.getX() * toScreen) < 2.0f;
            sizes << juce::String (ed->getWidth()) << "x" << juce::String (ed->getHeight()) << " ";
        }
        ed->setScale (5.0f);
        ok = ok && std::abs (ed->getScale() - CoherenceEditor::maxScale) < 1e-4f;
        ed->setScale (1.15f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        CoherenceProcessor p3;
        p3.setStateInformation (mb.getData(), (int) mb.getSize());
        std::unique_ptr<juce::AudioProcessorEditor> e3 (p3.createEditor());
        auto* ed3 = dynamic_cast<CoherenceEditor*> (e3.get());
        ok = ok && std::abs (ed3->getScale() - 1.15f) < 0.002f;
        check (ok, "window size: the whole panel scales with the window (" + sizes.trim() + "), limits hold, and the size is restored with the set");
        // New instances open at the size last chosen from the SETUP menu.
        settings->setDefaultScale (1.2f);
        CoherenceProcessor p4;
        std::unique_ptr<juce::AudioProcessorEditor> e4 (p4.createEditor());
        check (std::abs (dynamic_cast<CoherenceEditor*> (e4.get())->getScale() - 1.2f) < 0.002f, "a new instance opens at the size last chosen in the SETUP menu (120 %)");
        settings->setDefaultScale (1.0f);
        check (e1->isResizable(), "the window is resizable (host window or corner), aspect ratio locked");
    }
    e1.reset(); e2.reset();
    useDefaultTestSettings();
}

//==============================================================================
// v0.2.4: gain stepping from every control path. A host (automation, UC1/UF1 via 360 Link, any controller) writes
// parameters the way JUCE's VST3 and AU wrappers do: AudioProcessorParameter::setValue, then the listeners.
void gainSteppingExternal()
{
    line ("\n## Gain stepping from every control path (host, automation, controllers, recall)\n");
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("coherence-settings-ext");
    dir.createDirectory();
    UserSettings::setFileForTesting (dir.getChildFile ("COHERENCE.settings"));
    juce::SharedResourcePointer<UserSettings> settings;
    auto mode = [&] (GainStepping m) { settings->setGainStepping (m); settings->dispatchPendingMessages(); };

    const juce::StringArray dbIds { "lf_gain", "lmf_gain", "hmf_gain", "hf_gain", id::input, id::mid, id::side, id::threshold, id::makeup, id::output, id::eqOutput, "side_lf_gain", "side_lmf_gain", "side_hmf_gain", "side_hf_gain" };
    auto hostSet = [] (juce::RangedAudioParameter& q, float v)
    {
        auto& base = static_cast<juce::AudioProcessorParameter&> (q);
        const float n = q.convertTo0to1 (v);
        base.setValue (n);
        base.sendValueChangedMessageToListeners (n);
    };
    auto stored = [] (juce::RangedAudioParameter& q) { return q.convertFrom0to1 (q.getValue()); };
    auto playedOf = [] (const dsp::Settings& st, const juce::String& pid) -> double
    {
        for (int b = 0; b < 4; ++b) if (pid == id::bandGain (id::bands[b])) return st.bands[(size_t) b].gainDb;
        if (pid == id::input) return st.inputDb;
        if (pid == id::mid) return st.midDb;
        if (pid == id::side) return st.sideDb;
        if (pid == id::threshold) return st.comp.thresholdDb;
        if (pid == id::makeup) return st.comp.makeupDb;
        return st.outputDb;
    };

    // A host listener: counts every change the plug-in reports to the host.
    struct HostListener final : juce::AudioProcessorListener
    {
        int changes = 0, gestures = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++gestures; }
    };

    {   // 1. Played and displayed value per mode, for every dB control; the stored value is what the host sent.
        CoherenceProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());   // an open editor must not write back either
        HostListener host;
        p.addListener (&host);
        ParameterReader reader (p.state());
        struct Case { GainStepping m; float sent, plays; };
        const Case cases[] = { { GainStepping::fine, 1.37f, 1.4f }, { GainStepping::fine, -2.35f, -2.4f },
                               { GainStepping::standard, 1.37f, 1.5f }, { GainStepping::standard, 1.24f, 1.0f },
                               { GainStepping::standard, -1.25f, -1.5f }, { GainStepping::standard, 0.03f, 0.0f },
                               { GainStepping::precision, 1.37f, 1.37f } };
        bool ok = true;
        int writes = 0;
        juce::String detail;
        for (const auto& c : cases)
        {
            mode (c.m);
            for (const auto& pid : dbIds)
            {
                auto& q = *p.state().getParameter (pid);
                const float sent = pid == id::threshold ? c.sent - 20.0f : c.sent;
                const float expect = pid == id::threshold ? c.plays - 20.0f : c.plays;
                const float sentNorm = q.convertTo0to1 (sent);
                hostSet (q, sent);
                ++writes;
                const double played = playedOf (reader.read(), pid);
                const auto text = q.getText (q.getValue(), 32);
                const float shown = q.convertFrom0to1 (q.getValueForText (text));
                const bool thisOk = std::abs (played - expect) < 1e-5 && std::abs (shown - expect) < 1e-5
                                    && q.getValue() == sentNorm && std::abs (stored (q) - sent) < 1e-5f;
                if (! thisOk && detail.isEmpty())
                    detail = pid + " in " + gainSteppingLabel (c.m) + ": sent " + fmt (sent, 2) + ", plays " + fmt (played, 3) + ", shows " + text;
                ok = ok && thisOk;
            }
        }
        check (ok, "a host or controller value plays and displays on the step grid of the mode, for all 10 dB controls: 1.37 dB plays 1.4 (Fine), 1.5 (Standard), "
                   "1.37 (Precision); 1.24 plays 1.0 and -1.25 plays -1.5 (Standard, half away from zero)" + (detail.isEmpty() ? juce::String() : " (" + detail + ")"));
        check (host.changes == writes && host.gestures == 0,
               "no feedback: the plug-in reports nothing to the host beyond the host's own " + juce::String (writes) + " writes (" + juce::String (host.changes)
                   + " notifications, " + juce::String (host.gestures) + " gestures), with the editor open; the stored value is bit-exact what the host sent, "
                   "so a UC1/UF1 encoder always reads back its own value (no fighting, no automation written)");

        // The output fader's OFF position stays OFF.
        mode (GainStepping::standard);
        auto& out = *p.state().getParameter (id::output);
        hostSet (out, -60.0f);
        const double off = reader.read().outputDb;
        hostSet (out, 6.37f);
        const double six = reader.read().outputDb;
        check (off <= -59.995 && std::abs (six - 6.5) < 1e-5, "OUTPUT from the host: OFF stays OFF, 6.37 dB plays 6.5 dB (Standard)");

        // Changing the mode re-quantises host values without any write (nothing is sent to the host).
        auto& g = *p.state().getParameter ("lmf_gain");
        hostSet (g, 1.37f);
        const int before = host.changes;
        double perMode[3];
        int i = 0;
        for (auto m : { GainStepping::fine, GainStepping::standard, GainStepping::precision })
        {
            mode (m);
            perMode[i++] = reader.read().bands[1].gainDb;
        }
        check (std::abs (perMode[0] - 1.4) < 1e-5 && std::abs (perMode[1] - 1.5) < 1e-5 && std::abs (perMode[2] - 1.37) < 1e-5 && host.changes == before,
               "changing the mode in SETUP re-steps host values (1.37 dB plays " + fmt (perMode[0], 2) + " / " + fmt (perMode[1], 2) + " / " + fmt (perMode[2], 2)
                   + ") without writing anything to the host");

        // Fine adjustment is preserved: a GUI value (Shift drag or typed) plays exactly, also in Standard.
        mode (GainStepping::standard);
        auto* ed = dynamic_cast<CoherenceEditor*> (e.get());
        auto* k = ed->findKnob ("lmf_gain");
        hostSet (g, 1.0f);
        k->stepBy (3, true);   // Shift: +0.03 dB
        const double fineGui = reader.read().bands[1].gainDb;
        hostSet (g, stored (g));   // a host echoing the same value back
        const double afterEcho = reader.read().bands[1].gainDb;
        k->stepBy (1, false);  // a coarse step from 1.03 lands on the grid
        const double coarse = reader.read().bands[1].gainDb;
        // VST3 hosts pass every edit back to the processor in a later block, so during a Shift drag the echoes lag
        // behind the GUI (JUCE applies them as they arrive). They must not turn the panel value into a controller value.
        hostSet (g, 2.0f);
        std::vector<float> sentByGui;
        for (int i = 0; i < 6; ++i) { k->stepBy (1, true); sentByGui.push_back (stored (g)); }
        for (float v : sentByGui) hostSet (g, v);   // the echoes, late
        const double afterLagging = reader.read().bands[1].gainDb;
        check (std::abs (afterLagging - 2.06) < 1e-5, "a Shift drag whose VST3 echoes arrive late (2.01 ... 2.06 passed back after the drag) still plays exactly "
                                                         + fmt (afterLagging, 2) + " dB in Standard");
        check (std::abs (fineGui - 1.03) < 1e-5 && std::abs (afterEcho - 1.03) < 1e-5 && std::abs (coarse - 1.5) < 1e-5,
               "Shift fine adjustment from the GUI plays exactly in Standard (" + fmt (fineGui, 2) + " dB), a host echo of the same value keeps it, "
               "and a normal step lands on the grid (" + fmt (coarse, 2) + " dB)");
        p.removeListener (&host);
    }

    {   // 2. Automation: a host ramp plays on the grid, monotonic, never more than half a step away; no jitter.
        mode (GainStepping::standard);
        CoherenceProcessor p;
        ParameterReader reader (p.state());
        auto& g = *p.state().getParameter ("hmf_gain");
        bool mono = true, onGrid = true;
        double prev = -100.0, worst = 0.0;
        for (int i = -1500; i <= 1500; ++i)
        {
            const float v = (float) i / 100.0f;
            hostSet (g, v);
            const double played = reader.read().bands[2].gainDb;
            // Tolerance 1e-5 dB: on a grid point the stored value plays as is, and its float round-trip can sit up to
            // ~1e-6 dB below the exact grid value (clang/arm64). That is neither a step back nor off the grid.
            constexpr double tolDb = 1e-5;
            mono = mono && played >= prev - tolDb;
            onGrid = onGrid && std::abs (played - std::round (played * 2.0) / 2.0) < tolDb;
            worst = std::max (worst, std::abs (played - stored (g)));
            prev = played;
        }
        check (mono && onGrid && worst <= 0.25 + 1e-5, "a host ramp from -15 to +15 dB in 0.01 dB steps plays monotonic, on the 0.5 dB grid, at most "
                                                         + fmt (worst, 2) + " dB from the automation (no jitter, no back-and-forth)");

        // Audio: host automation in Standard sounds exactly like the same moves made on the grid from the GUI.
        CoherenceProcessor a, b;
        for (auto* x : { &a, &b }) x->prepareToPlay (48000.0, 64);
        std::mt19937 rng (11);
        std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        juce::MidiBuffer midi;
        double diff = 0.0;
        juce::String firstDiff;
        for (int blk = 0; blk < 400; ++blk)
        {
            const float v = -6.0f + 12.0f * (float) blk / 400.0f + 0.13f;   // off the grid
            hostSet (*a.state().getParameter ("lmf_gain"), v);
            hostSet (*a.state().getParameter (id::mid), -v * 0.5f);
            {
                const ScopedWriteSource gui (WriteSource::gui);
                // The grid value of what the host value is stored as (0.01 dB), set from the panel.
                auto onGrid = [] (juce::RangedAudioParameter& q, float x) { return q.convertTo0to1 (snapToGainStep (q.convertFrom0to1 (q.convertTo0to1 (x)), 0.5f)); };
                auto& gb = *b.state().getParameter ("lmf_gain");
                gb.setValueNotifyingHost (onGrid (gb, v));
                auto& mb = *b.state().getParameter (id::mid);
                mb.setValueNotifyingHost (onGrid (mb, -v * 0.5f));
            }
            juce::AudioBuffer<float> ba (2, 64), bb (2, 64);
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < 64; ++n) { const float x = u (rng); ba.setSample (c, n, x); bb.setSample (c, n, x); }
            {
                const auto sa = ParameterReader (a.state()).read(), sb = ParameterReader (b.state()).read();
                if ((sa.bands[1].gainDb != sb.bands[1].gainDb || sa.midDb != sb.midDb) && firstDiff.isEmpty())
                    firstDiff = "block " + juce::String (blk) + ": host plays " + juce::String (sa.bands[1].gainDb, 7) + " / " + juce::String (sa.midDb, 7)
                                + ", GUI " + juce::String (sb.bands[1].gainDb, 7) + " / " + juce::String (sb.midDb, 7);
            }
            a.processBlock (ba, midi);
            b.processBlock (bb, midi);
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < 64; ++n) diff = std::max (diff, (double) std::abs (ba.getSample (c, n) - bb.getSample (c, n)));
        }
        const double diffDb = 20.0 * std::log10 (diff + 1e-30);
        check (diffDb < -120.0, "audio: host automation off the grid (EQ gain and MID, 400 blocks) sounds the same as the same moves set on the 0.5 dB grid "
                                "from the GUI (largest difference " + (diff == 0.0 ? juce::String ("0, bit-identical") : fmt (diffDb, 1) + " dBFS, float rounding of the grid value") + ")" + (firstDiff.isEmpty() ? juce::String() : "; " + firstDiff));
    }

    {   // 3. Recall and A/B: a set plays exactly as it did when saved.
        mode (GainStepping::standard);
        CoherenceProcessor p;
        auto& g = *p.state().getParameter ("lf_gain");
        auto& m = *p.state().getParameter (id::mid);
        hostSet (g, 1.37f);   // from the UC1: plays 1.5
        {
            const ScopedWriteSource gui (WriteSource::gui);   // Shift-adjusted on the panel: plays 1.37
            m.setValueNotifyingHost (m.convertTo0to1 (1.37f));
        }
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        CoherenceProcessor r;
        r.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto st = ParameterReader (r.state()).read();
        const bool recallOk = std::abs (st.bands[0].gainDb - 1.5) < 1e-5 && std::abs (st.midDb - 1.37) < 1e-5
                              && std::abs (stored (*r.state().getParameter ("lf_gain")) - 1.37f) < 1e-5f;
        check (recallOk, "recall: a controller value plays on the grid again (1.37 stored, 1.5 played) and a panel fine value plays exactly (1.37), as when saved");

        // A set from v0.2.3 (no flags) plays exactly as stored, whatever the mode.
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        for (auto* c : xml->getChildIterator()) c->removeAttribute ("ext");
        if (auto* other = xml->getChildByName ("OTHER_SLOT"))
            if (auto* slot = other->getFirstChildElement())
                for (auto* c : slot->getChildIterator()) c->removeAttribute ("ext");
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        CoherenceProcessor o;
        o.setStateInformation (old.getData(), (int) old.getSize());
        check (std::abs (ParameterReader (o.state()).read().bands[0].gainDb - 1.37) < 1e-5,
               "a set saved before v0.2.4 (no stepping flags) plays exactly as stored (1.37 dB in Standard): old sessions sound unchanged");

        // A/B carries the flags: A = controller 1.37 (plays 1.5), B = panel 2.37 (plays 2.37).
        p.copyCurrentToOtherSlot();
        p.selectAbSlot (1);
        {
            const ScopedWriteSource gui (WriteSource::gui);
            g.setValueNotifyingHost (g.convertTo0to1 (2.37f));
        }
        p.selectAbSlot (0);
        const double aPlays = ParameterReader (p.state()).read().bands[0].gainDb;
        p.selectAbSlot (1);
        const double bPlays = ParameterReader (p.state()).read().bands[0].gainDb;
        p.selectAbSlot (0);
        // Rapid A/B toggling in a VST3 host: the echoes of B's values arrive after switching back to A.
        p.selectAbSlot (1);
        p.selectAbSlot (0);
        hostSet (g, 2.37f);   // B's value, passed back late
        hostSet (g, 1.37f);   // A's value, passed back
        const double afterToggle = ParameterReader (p.state()).read().bands[0].gainDb;
        check (std::abs (afterToggle - 1.5) < 1e-5 && dynamic_cast<SteppedGainParameter&> (g).isExternal(),
               "rapid A/B toggling with late VST3 echoes leaves A as it was (plays " + fmt (afterToggle, 2) + ")");
        check (std::abs (aPlays - 1.5) < 1e-5 && std::abs (bPlays - 2.37) < 1e-5,
               "A/B keeps each slot's stepping: A (controller 1.37) plays " + fmt (aPlays, 2) + ", B (panel 2.37) plays " + fmt (bPlays, 2));
    }
    mode (GainStepping::precision);   // what the rest of the measurements run in
    useDefaultTestSettings();
}

//==============================================================================
// v0.2.5: Weiss mode (DESIGN.md section 15).
struct WeissGolden { const char* name; Shape shape; double f0, q, g; double mag[11], ph[11]; };
// Measured from your Weiss EQ MP renders (REFERENCES/EQ renders v2/test results, 48 kHz): magnitude (dB) and phase
// (degrees) at 50, 100, 300, 1k, 3k, 6k, 10k, 14k, 17k, 19k, 20k Hz (Tools/../mac-results/weiss-model/golden.py).
const WeissGolden weissGolden[] = {
    { "WEISS BELL 10000 1 +6", Shape::bell, 10000, 1, 6, {-0.00020, 0.00070, 0.01032, 0.11936, 1.04412, 3.65733, 5.99951, 4.59583, 3.26967, 2.57598, 2.27973}, {0.2749, 0.5497, 1.6474, 5.4317, 14.7193, 18.9350, -0.0000, -16.4322, -19.3283, -19.2121, -18.7976} },
    { "WEISS BELL 15000 2 -9", Shape::bell, 15000, 2, -9, {-0.00057, -0.00079, -0.00305, -0.02887, -0.26413, -1.17139, -4.03583, -8.62716, -7.83310, -5.88346, -5.02559}, {-0.1595, -0.3189, -0.9568, -3.1907, -9.5971, -19.1419, -28.2869, -11.3481, 19.1177, 27.0618, 28.1179} },
    { "WEISS BELL 1000 1 +12", Shape::bell, 1000, 1, 12, {0.15808, 0.60597, 3.89930, 11.99950, 4.50082, 1.50117, 0.56621, 0.27594, 0.17512, 0.13230, 0.11504}, {8.4128, 16.1335, 34.4449, 0.0000, -35.6008, -24.3566, -15.6232, -11.0514, -8.8469, -7.7073, -7.1873} },
    { "WEISS HS 10K 2 +6", Shape::highShelf, 10000, 2, 6, {-0.00067, -0.00114, -0.00620, -0.06402, -0.58835, -1.69187, 6.35287, 7.53948, 7.03782, 6.78604, 6.68423}, {0.0744, 0.1488, 0.4478, 1.5417, 6.1418, 29.5444, 47.1781, 17.8536, 10.3963, 7.8825, 6.9618} },
    { "WEISS HS 15K 0.71 +9", Shape::highShelf, 15000, 0.71, 9, {-0.00056, -0.00067, -0.00190, -0.01558, -0.10865, 0.00805, 2.51444, 6.33206, 8.04674, 8.62864, 8.80348}, {0.1448, 0.2896, 0.8695, 2.9267, 9.5273, 23.5400, 43.7116, 44.1604, 35.8140, 29.9885, 27.2750} },
    { "WEISS 48K HS 3K 2 +12", Shape::highShelf, 3000, 2, 12, {-0.00670, -0.02525, -0.22586, -2.77357, 15.63056, 13.49062, 12.48648, 12.22674, 12.14167, 12.10632, 12.09244}, {0.4956, 0.9964, 3.1624, 20.6843, 70.7343, 11.0467, 4.9582, 3.1772, 2.4606, 2.1140, 1.9608} },
    { "WEISS 48K 100 4 +12", Shape::lowShelf, 100, 4, 12, {13.72381, 18.44461, -3.37010, -0.24659, -0.02721, -0.00703, -0.00275, -0.00156, -0.00117, -0.00101, -0.00094}, {-8.1483, -76.1915, -15.8061, -2.2376, -0.7009, -0.3451, -0.2019, -0.1390, -0.1102, -0.0957, -0.0890} },
    { "WEISS 48K 100 .5 -6", Shape::lowShelf, 100, 0.5, -6, {-5.29204, -3.60928, -0.63015, -0.05681, -0.00671, -0.00202, -0.00102, -0.00075, -0.00066, -0.00062, -0.00061}, {14.1318, 21.2018, 13.4356, 4.2724, 1.4258, 0.7063, 0.4139, 0.2849, 0.2259, 0.1961, 0.1825} },
    { "WEISS LS 3OO 0.71 -3", Shape::lowShelf, 300, 0.71, -3, {-2.98900, -2.93640, -1.72400, -0.07755, -0.00592, -0.00176, -0.00093, -0.00070, -0.00063, -0.00060, -0.00059}, {2.3249, 4.8351, 12.9419, 5.2013, 1.6398, 0.8069, 0.4722, 0.3249, 0.2576, 0.2235, 0.2081} },
};
const double weissGoldenFreqs[11] = { 50, 100, 300, 1000, 3000, 6000, 10000, 14000, 17000, 19000, 20000 };

//==============================================================================
static void setPlain (CoherenceProcessor& p, const char* parameterId, float plainValue)
{
    auto* q = p.state().getParameter (parameterId);
    q->setValueNotifyingHost (q->convertTo0to1 (plainValue));
}

//==============================================================================
// NaN and Inf in the input (DESIGN.md section 21). One such sample used to stay in the filters, detectors and meters for
// good, even with EQ and DYN out and in bypass. The reference is a second instance that got 0 in place of the bad sample:
// every output sample must then be bit-identical to it, which also shows that nothing else about the processing changed.
void nonFiniteChecks()
{
    line ("\n## NaN and Inf in the input\n");
    struct Config { bool weiss, eq, dyn, ms, compFirst, bypass; };
    const auto setUp = [] (CoherenceProcessor& p, const Config& c, double sr, int block, int channels)
    {
        p.setPlayConfigDetails (channels, channels, sr, block);
        p.state().getParameter (id::eqMode)->setValueNotifyingHost (c.weiss ? 1.0f : 0.0f);
        p.state().getParameter (id::eqOn)->setValueNotifyingHost (c.eq ? 1.0f : 0.0f);
        p.state().getParameter (id::dynOn)->setValueNotifyingHost (c.dyn ? 1.0f : 0.0f);
        p.state().getParameter (id::eqMs)->setValueNotifyingHost (c.ms ? 1.0f : 0.0f);
        p.state().getParameter (id::order)->setValueNotifyingHost (c.compFirst ? 1.0f : 0.0f);
        p.state().getParameter (id::bypass)->setValueNotifyingHost (c.bypass ? 1.0f : 0.0f);
        setPlain (p, "hf_gain", 6.0f);
        setPlain (p, "lf_gain", -3.0f);
        setPlain (p, "side_hf_gain", -4.0f);
        setPlain (p, id::threshold, -24.0f);
        p.prepareToPlay (sr, block);
    };
    const auto noise = [] (juce::AudioBuffer<float>& b, juce::Random& r)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (c, i, 0.25f * (r.nextFloat() * 2.0f - 1.0f));
    };
    const auto metersFinite = [] (dsp::MeterData& m)
    {
        bool ok = std::isfinite (m.gainReductionDb.load()) && std::isfinite (m.hostGainReductionDb.load()) && std::isfinite (m.midRms.load()) && std::isfinite (m.sideRms.load());
        for (size_t c = 0; c < 2; ++c)
            ok = ok && std::isfinite (m.inPeak[c].load()) && std::isfinite (m.outPeak[c].load()) && std::isfinite (m.inRms[c].load()) && std::isfinite (m.outRms[c].load());
        return ok;
    };
    struct Result { long differ = 0, nonFinite = 0; bool meters = true; };
    // `hit` gets `bad` where `ref` gets 0; everything else is the same. flipBypassAt: the bypass goes off at that block.
    const auto compare = [&] (const Config& c, float bad, int channelMask, int badBlock, int badSample, int badLength, int blocks, int block, double sr, int channels, int flipBypassAt)
    {
        Result out;
        CoherenceProcessor hit, ref;
        setUp (hit, c, sr, block, channels);
        setUp (ref, c, sr, block, channels);
        juce::AudioBuffer<float> a (channels, block), b (channels, block);
        juce::MidiBuffer midi;
        juce::Random ra (5), rb (5);
        for (int k = 0; k < blocks; ++k)
        {
            noise (a, ra);
            noise (b, rb);
            if (k >= badBlock && k < badBlock + badLength)
                for (int ch = 0; ch < channels; ++ch)
                    if ((channelMask >> ch) & 1)
                        for (int i = badSample < 0 ? 0 : badSample; i < (badSample < 0 ? block : badSample + 1); ++i)
                        {
                            a.setSample (ch, i, bad);
                            b.setSample (ch, i, 0.0f);
                        }
            if (k == flipBypassAt)
            {
                hit.state().getParameter (id::bypass)->setValueNotifyingHost (0.0f);
                ref.state().getParameter (id::bypass)->setValueNotifyingHost (0.0f);
            }
            hit.processBlock (a, midi);
            ref.processBlock (b, midi);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float x = a.getSample (ch, i), y = b.getSample (ch, i);
                    out.nonFinite += std::isfinite (x) ? 0 : 1;
                    out.differ += std::memcmp (&x, &y, sizeof (float)) == 0 ? 0 : 1;
                }
            out.meters = out.meters && metersFinite (hit.meters()) && std::isfinite (hit.gainReductionMeter().get());
        }
        return out;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();

    {   // Every configuration, and each kind of bad sample.
        long differ = 0, nonFinite = 0; bool meters = true; int runs = 0;
        juce::String firstProblem;
        for (int m = 0; m < 64; ++m)
        {
            const Config c { (m & 1) != 0, (m & 2) != 0, (m & 4) != 0, (m & 8) != 0, (m & 16) != 0, (m & 32) != 0 };
            int kind = 0;
            for (const float bad : { nan, inf, -inf })
            {
                const auto r = compare (c, bad, kind == 0 ? 1 : kind == 1 ? 2 : 3, 10, 100, 1, 40, 512, 48000.0, 2, -1);
                if ((r.differ != 0 || r.nonFinite != 0 || ! r.meters) && firstProblem.isEmpty())
                    firstProblem = juce::String (c.weiss ? "Weiss" : "COHERENCE") + (c.eq ? ", EQ in" : ", EQ out") + (c.dyn ? ", DYN in" : ", DYN out") + (c.ms ? ", M/S" : ", stereo") + (c.compFirst ? ", DYN > EQ" : ", EQ > DYN") + (c.bypass ? ", bypassed" : "")
                                   + ": " + juce::String (r.nonFinite) + " bad samples, " + juce::String (r.differ) + " different";
                differ += r.differ;
                nonFinite += r.nonFinite;
                meters = meters && r.meters;
                ++runs;
                ++kind;
            }
        }
        check (nonFinite == 0, "no NaN or Inf ever reaches the output: both EQ modes, EQ and DYN in and out, stereo and M/S, both orders, bypass, " + juce::String (runs) + " runs (" + juce::String (nonFinite) + " bad samples)" + (firstProblem.isEmpty() ? juce::String() : "; first problem: " + firstProblem));
        check (differ == 0, "after a bad sample the output is bit-identical to a run that got 0 there, so nothing stays in the state and recovery is immediate (" + juce::String (differ) + " samples differ)");
        check (meters, "the meters and the control-surface meter stay finite");
    }
    {   // Bypass: the bad sample arrives while bypassed, and the bypass then goes off.
        long differ = 0, nonFinite = 0;
        for (const bool weiss : { false, true })
            for (const bool ms : { false, true })
                for (const float bad : { nan, inf })
                {
                    const auto r = compare ({ weiss, true, true, ms, false, true }, bad, 3, 10, 100, 1, 60, 512, 48000.0, 2, 30);
                    differ += r.differ;
                    nonFinite += r.nonFinite;
                }
        check (differ == 0 && nonFinite == 0, "a bad sample during bypass leaves nothing behind when the bypass goes off (" + juce::String (nonFinite) + " bad, " + juce::String (differ) + " different)");
    }
    {   // Long stretches of bad samples, every block size, other sample rates, a mono track.
        long differ = 0, nonFinite = 0;
        const auto add = [&] (const Result& r) { differ += r.differ; nonFinite += r.nonFinite; };
        for (const bool weiss : { false, true })
        {
            const Config c { weiss, true, true, false, false, false };
            add (compare (c, nan, 3, 10, 7, 20, 60, 512, 48000.0, 2, -1));     // the same sample of 20 blocks in a row
            for (const int block : { 1, 7, 31, 32, 33, 64, 4096 })
                add (compare (c, nan, 3, block == 1 ? 3000 : (block < 64 ? 200 : 4), block - 1, 1, block == 1 ? 6000 : (block < 64 ? 400 : (block == 64 ? 200 : 8)), block, 48000.0, 2, -1));
            for (const double sr : { 44100.0, 88200.0, 96000.0, 192000.0 })
                add (compare (c, inf, 3, 10, 0, 1, 30, 512, sr, 2, -1));
            add (compare (c, nan, 1, 10, 100, 1, 30, 512, 48000.0, 1, -1));    // mono
        }
        check (differ == 0 && nonFinite == 0, "the same for 20 bad blocks in a row, block sizes 1 to 4096, 44.1 to 192 kHz, and mono (" + juce::String (nonFinite) + " bad, " + juce::String (differ) + " different)");
    }
    {   // Whole blocks of bad samples, then normal audio: the same as if they had been silence.
        long differ = 0, nonFinite = 0;
        for (const bool weiss : { false, true })
            for (const float bad : { nan, inf })
            {
                const auto r = compare ({ weiss, true, true, false, false, false }, bad, 3, 10, -1, 3, 60, 512, 48000.0, 2, -1);
                differ += r.differ;
                nonFinite += r.nonFinite;
            }
        check (differ == 0 && nonFinite == 0, "whole blocks of NaN or Inf: the same as silence, and normal audio afterwards is unaffected (" + juce::String (nonFinite) + " bad, " + juce::String (differ) + " different)");
    }
    {   // Garbage finite values (beyond +120 dBFS) are bad samples too: before 0.6.1 one 1e30 sample left AUTO makeup
        // wrong for over six minutes (mac-results/v0.6.1/huge-finite-before-limit.md).
        long differ = 0, nonFinite = 0;
        for (const bool weiss : { false, true })
            for (const float bad : { 1.0e30f, -std::numeric_limits<float>::max(), 1.5e6f })
            {
                const auto r = compare ({ weiss, true, true, false, false, false }, bad, 3, 10, 100, 1, 60, 512, 48000.0, 2, -1);
                differ += r.differ;
                nonFinite += r.nonFinite;
            }
        check (differ == 0 && nonFinite == 0, "a garbage value beyond +120 dBFS (1.5e6, 1e30, the largest float) is the same as silence there, with AUTO makeup on (" + juce::String (differ) + " different)");
        const auto r = compare ({ true, true, true, false, false, false }, dsp::maxInputSample, 3, 10, 100, 1, 12, 512, 48000.0, 2, -1);
        check (r.differ > 0 && r.nonFinite == 0, "a sample of exactly +120 dBFS is still processed as audio");
    }
    {   // Not from the input: a host writing NaN to a parameter. The engine must start clean by itself once the value is good.
        for (const bool weiss : { false, true })
        {
            CoherenceProcessor p;
            setUp (p, { weiss, true, true, false, false, false }, 48000.0, 512, 2);
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer midi;
            juce::Random r (2);
            for (int k = 0; k < 20; ++k) { noise (b, r); p.processBlock (b, midi); }
            auto* gain = p.state().getParameter ("hf_gain");
            long badOut = 0;
            gain->setValueNotifyingHost (nan);
            for (int k = 0; k < 20; ++k)
            {
                noise (b, r);
                p.processBlock (b, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        badOut += std::isfinite (b.getSample (ch, i)) ? 0 : 1;
            }
            gain->setValueNotifyingHost (0.7f);
            float after = 0.0f;
            for (int k = 0; k < 60; ++k)
            {
                noise (b, r);
                p.processBlock (b, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        badOut += std::isfinite (b.getSample (ch, i)) ? 0 : 1;
                after = b.getRMSLevel (0, 0, 512);
            }
            check (badOut == 0 && after > 0.01f, juce::String (weiss ? "Weiss" : "COHERENCE") + " mode: NaN written to a parameter never reaches the output, and the sound returns once the value is good (level " + fmt (after, 3) + ")");
        }
    }
    {   // The guard only reads: a clean run through the plug-in equals the engine fed directly, bit for bit, in both modes.
        for (const bool weiss : { false, true })
        {
            CoherenceProcessor p;
            setUp (p, { weiss, true, true, true, false, false }, 48000.0, 512, 2);
            juce::AudioBuffer<float> a (2, 512);
            juce::MidiBuffer midi;
            juce::Random r (4);
            long notFinite = 0;
            for (int k = 0; k < 200; ++k)
            {
                noise (a, r);
                p.processBlock (a, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        notFinite += std::isfinite (a.getSample (ch, i)) ? 0 : 1;
            }
            check (notFinite == 0 && p.getLatencySamples() == (weiss ? weissLatencySamples : 0), juce::String (weiss ? "Weiss" : "COHERENCE") + " mode: the latency stays " + juce::String (p.getLatencySamples()) + " samples");
        }
    }
}

//==============================================================================
// New instances start in Weiss mode; saved sets keep the mode they were saved in.
void defaultEqModeChecks()
{
    line ("\n## Weiss as the default\n");
    const auto modeOf = [] (CoherenceProcessor& p) { return p.state().getParameter (id::eqMode)->getValue() >= 0.5f; };
    {
        CoherenceProcessor p;
        check (modeOf (p) && p.getLatencySamples() == weissLatencySamples, "a new instance is in Weiss mode, and reports 15 samples before it is prepared");
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        check (p.getLatencySamples() == weissLatencySamples, "and still 15 after it is prepared");
        juce::AudioBuffer<float> b (2, 512);
        b.clear();
        b.setSample (0, 0, 1.0f);
        juce::MidiBuffer midi;
        p.processBlock (b, midi);
        int peak = 0;
        for (int i = 0; i < 512; ++i)
            if (std::abs (b.getSample (0, i)) > 0.5f)
                peak = i;
        check (peak == weissLatencySamples, "with no switch and no fade: an impulse in the first block comes out at sample " + juce::String (peak));
    }
    const auto savedInMode = [] (bool weiss)
    {
        CoherenceProcessor p;
        p.state().getParameter (id::eqMode)->setValueNotifyingHost (weiss ? 1.0f : 0.0f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        return mb;
    };
    {
        const auto coh = savedInMode (false), wei = savedInMode (true);
        CoherenceProcessor a, b;
        a.setStateInformation (coh.getData(), (int) coh.getSize());
        b.setStateInformation (wei.getData(), (int) wei.getSize());
        check (! modeOf (a) && a.getLatencySamples() == 0, "a set saved in COHERENCE mode opens in COHERENCE mode, reporting 0 samples at once");
        check (modeOf (b) && b.getLatencySamples() == weissLatencySamples, "a set saved in Weiss mode opens in Weiss mode, reporting 15 samples");
        a.selectAbSlot (1);
        check (! modeOf (a), "and the COHERENCE set's other slot stays in COHERENCE mode too");
    }
    {   // From before the Weiss mode: neither slot has an EQ mode at all.
        auto mb = savedInMode (true);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        const auto strip = [] (juce::XmlElement& e)
        {
            for (auto* c : e.getChildIterator())
                if (c->getStringAttribute ("id") == id::eqMode) { e.removeChildElement (c, true); break; }
        };
        strip (*xml);
        if (auto* other = xml->getChildByName ("OTHER_SLOT"))
            for (auto* c : other->getChildIterator())
                strip (*c);
        xml->removeAttribute ("otherSlotFresh");
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        CoherenceProcessor o;
        o.setStateInformation (old.getData(), (int) old.getSize());
        check (! modeOf (o) && o.getLatencySamples() == 0, "a set from before the Weiss mode opens in COHERENCE mode, not in the new default");
        o.selectAbSlot (1);
        check (! modeOf (o), "in both slots");
    }
    {   // Switching by hand still works.
        CoherenceProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer midi;
        const auto block = [&] { b.clear(); p.processBlock (b, midi); };
        block();
        p.state().getParameter (id::eqMode)->setValueNotifyingHost (0.0f);
        block();
        const int toCoherence = p.getLatencySamples();
        p.state().getParameter (id::eqMode)->setValueNotifyingHost (1.0f);
        block();
        check (toCoherence == 0 && p.getLatencySamples() == weissLatencySamples, "switching between the modes by hand still works: 15, 0, 15 samples");
    }
}

//==============================================================================
// Saved sets with values that aren't finite numbers (v0.6.2, second candidate). Such a value loads its parameter at the
// default; every finite value loads as it always has. A set built here is what getStateInformation() writes: PARAM
// children with "id" and "value", an OTHER_SLOT child holding the other A/B slot, and the window scale.
void savedStateValueChecks()
{
    line ("\n## Saved sets with values that aren't numbers\n");
    using Values = std::map<juce::String, juce::String>;   // parameter ID -> the text stored as its value
    const auto savedParams = [] (CoherenceProcessor& p)
    {
        std::vector<juce::RangedAudioParameter*> v;
        for (auto* q : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q); rp != nullptr && isSavedParameter (*rp))
                v.push_back (rp);
        return v;
    };
    // A slot's tree: every parameter at its default, except those given in `values`.
    const auto slotTree = [&] (CoherenceProcessor& p, const Values& values, const juce::String& all = {})
    {
        juce::ValueTree t ("COHERENCE");
        for (auto* rp : savedParams (p))
        {
            juce::ValueTree c ("PARAM");
            c.setProperty ("id", rp->getParameterID(), nullptr);
            if (const auto it = values.find (rp->getParameterID()); it != values.end()) c.setProperty ("value", it->second, nullptr);
            else if (all.isNotEmpty())                                                 c.setProperty ("value", all, nullptr);
            else                                                                       c.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    };
    const auto toBinary = [] (juce::ValueTree main, const juce::ValueTree& other)
    {
        if (other.isValid())
        {
            juce::ValueTree o ("OTHER_SLOT");
            o.appendChild (other.createCopy(), nullptr);
            main.appendChild (o, nullptr);
        }
        juce::MemoryBlock mb;
        if (auto xml = main.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        return mb;
    };
    const auto load = [] (CoherenceProcessor& p, const juce::MemoryBlock& mb)
    {
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
    };
    const auto allFinite = [&] (CoherenceProcessor& p)
    {
        for (auto* rp : savedParams (p))
            if (! std::isfinite (rp->getValue()) || ! std::isfinite (rp->convertFrom0to1 (rp->getValue())))
                return false;
        return true;
    };
    const auto atDefaults = [&] (CoherenceProcessor& p)
    {
        int n = 0;
        for (auto* rp : savedParams (p))
            if (std::abs (rp->getValue() - rp->getDefaultValue()) < 1.0e-6f)
                ++n;
        return n;
    };
    const auto audio = [] (CoherenceProcessor& p)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer midi;
        std::mt19937 rng (99);
        std::uniform_real_distribution<float> noise (-0.25f, 0.25f);
        for (int i = 0; i < 100; ++i)
        {
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < 512; ++n)
                    b.setSample (c, n, noise (rng));
            p.processBlock (b, midi);
            out.insert (out.end(), b.getReadPointer (0), b.getReadPointer (0) + 512);
            out.insert (out.end(), b.getReadPointer (1), b.getReadPointer (1) + 512);
        }
        return out;
    };
    const auto peakOf = [] (const std::vector<float>& v) { float m = 0.0f; for (float x : v) m = std::max (m, std::abs (x)); return m; };
    const auto textOf = [] (const juce::MemoryBlock& mb) { const auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()); return xml != nullptr ? xml->toString().toLowerCase() : juce::String(); };

    CoherenceProcessor fresh;
    load (fresh, {});
    const auto freshAudio = audio (fresh);
    const int numSaved = (int) savedParams (fresh).size();

    // Every value bad, in the settings in use. Up to the first candidate, NaN made 44 of the 53 parameters NaN, the output
    // silent, and the next save wrote it back; an infinity put every control at one end of its travel.
    {
        bool finite = true, defaults = true, sameAudio = true, cleanSave = true;
        juce::StringArray tried;
        for (const char* bad : { "nan", "-nan", "NaN", "inf", "-inf", "Infinity", "1e400", "-1e400", "1e39" })
        {
            CoherenceProcessor p;
            load (p, toBinary (slotTree (p, {}, bad), {}));
            finite = finite && allFinite (p);
            defaults = defaults && atDefaults (p) == numSaved;
            sameAudio = sameAudio && audio (p) == freshAudio;
            juce::MemoryBlock again;
            p.getStateInformation (again);
            const auto text = textOf (again);
            cleanSave = cleanSave && text.isNotEmpty() && ! text.contains ("nan") && ! text.contains ("inf");
            tried.add (bad);
        }
        check (finite && defaults, "a set whose every value is " + tried.joinIntoString (", ") + ": all " + juce::String (numSaved) + " parameters load finite, at their defaults");
        check (sameAudio && peakOf (freshAudio) > 0.1f, "and it then sounds exactly like a new instance: bit-identical output, peak " + fmt (peakOf (freshAudio), 3) + " (it was silent)");
        check (cleanSave, "and saving it again writes no such value");
    }

    // One bad value among good ones: only that parameter falls back.
    {
        CoherenceProcessor source;
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> any (0.0f, 1.0f);
        Values good;
        for (auto* rp : savedParams (source))
            good[rp->getParameterID()] = juce::String (rp->convertFrom0to1 (any (rng)), 9);
        CoherenceProcessor reference;
        load (reference, toBinary (slotTree (reference, good), {}));
        auto damaged = good;
        damaged["hf_gain"] = "nan";
        damaged[id::threshold] = "-inf";
        damaged["lmf_freq"] = "inf";
        CoherenceProcessor p;
        load (p, toBinary (slotTree (p, damaged), {}));
        int same = 0, fellBack = 0;
        const auto ref = savedParams (reference);
        const auto got = savedParams (p);
        for (size_t i = 0; i < got.size(); ++i)
        {
            if (damaged.at (got[i]->getParameterID()) != good.at (got[i]->getParameterID()))
                fellBack += std::abs (got[i]->getValue() - got[i]->getDefaultValue()) < 1.0e-6f ? 1 : 0;
            else
                same += juce::exactlyEqual (got[i]->getValue(), ref[i]->getValue()) ? 1 : 0;
        }
        check (fellBack == 3 && same == numSaved - 3, "three bad values among good ones: those three load at their defaults, and the other " + juce::String (same) + " exactly as saved");
    }

    // The other A/B slot: its values reach the parameters when it is selected.
    {
        bool finite = true, defaults = true, backExact = true;
        for (const char* bad : { "nan", "inf", "-inf" })
        {
            CoherenceProcessor p;
            Values a;
            a["hf_gain"] = "6";
            a[id::threshold] = "-30";
            load (p, toBinary (slotTree (p, a), slotTree (p, {}, bad)));
            std::vector<float> slotA;
            for (auto* rp : savedParams (p)) slotA.push_back (rp->getValue());
            p.toggleAbSlot();
            finite = finite && allFinite (p);
            defaults = defaults && atDefaults (p) == numSaved;
            p.toggleAbSlot();
            const auto now = savedParams (p);
            for (size_t i = 0; i < now.size(); ++i)
                backExact = backExact && juce::exactlyEqual (now[i]->getValue(), slotA[i]);
        }
        check (finite && defaults && backExact, "bad values in the other A/B slot: switching to it gives finite settings at their defaults, and switching back returns the first slot exactly");
    }

    // Where such a set comes from in practice: a host writes NaN to a parameter, and the set is saved.
    {
        CoherenceProcessor p;
        load (p, {});
        setPlain (p, id::threshold, -27.0f);
        p.state().getParameter ("hf_gain")->setValueNotifyingHost (std::numeric_limits<float>::quiet_NaN());
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        CoherenceProcessor q;
        load (q, saved);
        auto* hf = q.state().getParameter ("hf_gain");
        auto* th = q.state().getParameter (id::threshold);
        check (textOf (saved).contains ("nan") && allFinite (q) && std::abs (hf->getValue() - hf->getDefaultValue()) < 1.0e-6f
                   && std::abs (th->convertFrom0to1 (th->getValue()) + 27.0f) < 0.01f && peakOf (audio (q)) > 0.01f,
               "a set saved after a host wrote NaN to HF GAIN reopens with HF GAIN at its default, THRESHOLD as saved (" + th->getCurrentValueAsText() + "), and audio passing");
    }

    // The window scale.
    {
        bool unset = true;
        for (const char* bad : { "nan", "inf", "-inf", "1e400" })
        {
            CoherenceProcessor p;
            juce::ValueTree t ("COHERENCE");
            t.setProperty ("uiScale", juce::String (bad), nullptr);
            load (p, toBinary (t, {}));
            unset = unset && juce::exactlyEqual (p.getUiScale(), -1.0f);
        }
        CoherenceProcessor p;
        juce::ValueTree t ("COHERENCE");
        t.setProperty ("uiScale", 1.4f, nullptr);
        load (p, toBinary (t, {}));
        check (unset && std::abs (p.getUiScale() - 1.4f) < 1.0e-6f, "a window scale that isn't a number counts as not chosen; a real one (1.4) is kept");
    }

    // Finite values are left alone: in range as saved, and out of range at the nearest end, as before.
    {
        CoherenceProcessor p;
        Values v;
        v["hf_gain"] = "99";
        v["lf_gain"] = "-99";
        v["hmf_freq"] = "-5";
        v[id::makeup] = "1e30";
        v["lmf_gain"] = "3.25";
        load (p, toBinary (slotTree (p, v), {}));
        const auto dB = [&] (const char* pid) { auto* q = p.state().getParameter (pid); return q->convertFrom0to1 (q->getValue()); };
        check (std::abs (dB ("hf_gain") - 15.0f) < 1.0e-4f && std::abs (dB ("lf_gain") + 15.0f) < 1.0e-4f && std::abs (dB ("hmf_freq") - 300.0f) < 1.0e-2f
                   && std::abs (dB (id::makeup) - 24.0f) < 1.0e-4f && std::abs (dB ("lmf_gain") - 3.25f) < 1.0e-4f,
               "finite values load as before: 3.25 dB stays 3.25, and values beyond a range go to its nearest end (99 dB to +15, -99 to -15, -5 Hz to 300 Hz, 1e30 dB to +24)");
    }

    // A valid set, every setting away from its default, B different from A, stepping flags and a window scale: unchanged
    // by all of this, to the byte.
    {
        CoherenceProcessor p;
        load (p, {});
        std::mt19937 rng (11);
        std::uniform_real_distribution<float> any (0.0f, 1.0f);
        for (auto* rp : savedParams (p)) rp->setValueNotifyingHost (any (rng));   // from the host: the dB values carry the stepping flag
        { const ScopedWriteSource gui (WriteSource::gui); p.state().getParameter ("lf_gain")->setValueNotifyingHost (0.61f); }
        p.toggleAbSlot();
        for (auto* rp : savedParams (p)) rp->setValueNotifyingHost (any (rng));
        p.setUiScale (1.4f);
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        CoherenceProcessor q;
        load (q, saved);
        juce::MemoryBlock again;
        q.getStateInformation (again);
        int same = 0;
        const auto was = savedParams (p);
        const auto is = savedParams (q);
        for (size_t i = 0; i < is.size(); ++i)
            same += std::abs (is[i]->convertFrom0to1 (is[i]->getValue()) - was[i]->convertFrom0to1 (was[i]->getValue())) < 1.0e-6f ? 1 : 0;
        check (again == saved && same == numSaved && q.getAbSlot() == 1 && std::abs (q.getUiScale() - 1.4f) < 1.0e-6f,
               "a valid set round trip: " + juce::String (same) + " of " + juce::String (numSaved) + " settings, the slot and the window scale return, and saving again gives the same "
                   + juce::String ((int) saved.getSize()) + " bytes");
    }
}

//==============================================================================
// The undo history while a host plays automation (v0.6.2). The value tree state records every changed value through the
// undo manager, from a timer on the message thread. copyState() does that recording at once, and the undo manager's
// pending notification is delivered by hand, so these checks need no running message loop.
void undoHistoryChecks()
{
    line ("\n## Undo history and host automation\n");
    CoherenceProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    auto& um = p.undo();
    auto* gain = p.state().getParameter ("hf_gain");
    auto* thr = p.state().getParameter (id::threshold);
    auto* freq = p.state().getParameter ("lmf_freq");
    auto* mid = p.state().getParameter (id::mid);
    auto* side = p.state().getParameter (id::side);
    const auto record = [&] { (void) p.state().copyState(); um.dispatchPendingMessages(); };
    // A move on the panel, the way a control makes one (juce::ParameterAttachment): a new step, then the value.
    const auto panelMove = [&] (juce::RangedAudioParameter* q, float normalised)
    {
        um.beginNewTransaction();
        const ScopedWriteSource gui (WriteSource::gui);
        q->beginChangeGesture();
        q->setValueNotifyingHost (normalised);
        q->endChangeGesture();
    };
    record();
    um.clearUndoHistory();

    // The panel's own undo, as it has always been.
    {
        const float start = gain->getValue();
        panelMove (gain, 0.9f);
        record();
        const float moved = gain->getValue();
        p.undoStep (false);
        const float undone = gain->getValue();
        p.undoStep (true);
        check (std::abs (moved - 0.9f) < 1.0e-3f && std::abs (undone - start) < 1.0e-6f && std::abs (gain->getValue() - moved) < 1.0e-6f,
               "a panel move is one undo step: UNDO returns the control exactly and REDO brings the move back (" + fmt (start, 6) + " to " + fmt (moved, 6) + ", undone " + fmt (undone, 6) + ", redone " + fmt (gain->getValue(), 6) + ", normalised)");
    }

    // UNDO straight after a move, before the value tree state's timer has recorded it (the timer slows to twice a
    // second while nothing moves). Up to v0.6.1 that move was passed over and the one before it was taken back.
    {
        um.clearUndoHistory();
        panelMove (thr, 0.3f);
        record();
        const float thrNow = thr->getValue(), gainNow = gain->getValue();
        panelMove (gain, gainNow > 0.5f ? 0.1f : 0.9f);   // not recorded yet
        p.undoStep (false);
        check (std::abs (gain->getValue() - gainNow) < 1.0e-6f && std::abs (thr->getValue() - thrNow) < 1.0e-6f,
               "UNDO pressed before a move has been recorded still takes back that move (HF GAIN back at " + fmt (gain->getValue(), 3)
                   + "), not the one before it (THRESHOLD stays at " + fmt (thr->getValue(), 3) + ")");
        p.undoStep (true);
        check (std::abs (gain->getValue() - gainNow) > 0.1f, "and REDO brings it back");
    }

    // A new instance, with nothing loaded: the first move of a control is a step like any other. From v0.5.0 to v0.6.1
    // it wasn't recorded for most controls, and UNDO then took back an earlier move of another control.
    {
        CoherenceProcessor fresh;
        fresh.setPlayConfigDetails (2, 2, 48000.0, 512);
        fresh.prepareToPlay (48000.0, 512);
        auto& fum = fresh.undo();
        const auto rec = [&] { (void) fresh.state().copyState(); fum.dispatchPendingMessages(); };
        const auto move = [&] (juce::RangedAudioParameter* q, float normalised)
        {
            fum.beginNewTransaction();
            const ScopedWriteSource gui (WriteSource::gui);
            q->beginChangeGesture();
            q->setValueNotifyingHost (normalised);
            q->endChangeGesture();
        };
        rec();
        auto* threshold = fresh.state().getParameter (id::threshold);
        auto* hf = fresh.state().getParameter ("hf_gain");
        move (threshold, 0.3f);
        rec();
        move (threshold, 0.6f);
        rec();
        const float thresholdNow = threshold->getValue(), hfStart = hf->getValue();
        move (hf, 0.9f);
        rec();
        fresh.undoStep (false);
        check (std::abs (hf->getValue() - hfStart) < 1.0e-6f && std::abs (threshold->getValue() - thresholdNow) < 1.0e-6f,
               "new instance: UNDO after the first move of HF GAIN takes that move back (to " + fmt (hf->getValue(), 3) + "), and leaves THRESHOLD where it was ("
                   + fmt (threshold->getValue(), 3) + ")");

        int total = 0, undone = 0, redone = 0;
        juce::StringArray kept, notRedone;
        CoherenceProcessor each;
        each.setPlayConfigDetails (2, 2, 48000.0, 512);
        each.prepareToPlay (48000.0, 512);
        for (auto* a : each.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (a); rp != nullptr && isSavedParameter (*rp))
            {
                const float before = rp->getValue();
                each.undo().beginNewTransaction();
                {
                    const ScopedWriteSource gui (WriteSource::gui);
                    rp->beginChangeGesture();
                    // A legal value, as a control on the panel sets: a switch goes to exactly 0 or 1.
                    rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (before > 0.5f ? 0.1f : 0.9f)));
                    rp->endChangeGesture();
                }
                (void) each.state().copyState();
                const float after = rp->getValue();
                const bool moved = std::abs (after - before) > 0.1f;
                each.undoStep (false);
                ++total;
                if (moved && std::abs (rp->getValue() - before) < 1.0e-6f) ++undone; else kept.add (rp->getParameterID());
                each.undoStep (true);
                if (std::abs (rp->getValue() - after) < 1.0e-6f) ++redone; else notRedone.add (rp->getParameterID());
            }
        check (undone == total, "new instance: the first move of every control can be undone (" + juce::String (undone) + " of " + juce::String (total)
                                    + (kept.isEmpty() ? juce::String() : "; not undone: " + kept.joinIntoString (" ")) + ")");
        check (redone == total, "and redone (" + juce::String (redone) + " of " + juce::String (total)
                                    + (notRedone.isEmpty() ? juce::String() : "; not redone: " + notRedone.joinIntoString (" ")) + ")");
    }

    // One automated parameter: successive values merge into a single entry.
    {
        um.clearUndoHistory();
        int most = 0;
        for (int i = 0; i < 3000; ++i)
        {
            gain->setValueNotifyingHost (0.5f + 0.4f * std::sin (0.01f * (float) i));   // as a host writes automation: no gesture
            record();
            most = juce::jmax (most, um.getNumActionsInCurrentTransaction());
        }
        check (most <= 1, "automation of one parameter is one undo entry however long it plays (most held: " + juce::String (most) + ")");
    }

    // Several automated parameters: their entries can't merge. Up to v0.6.1 the open step kept every one of them for as
    // long as the instance lived.
    {
        um.clearUndoHistory();
        int most = 0;
        for (int i = 0; i < 20000; ++i)
        {
            gain->setValueNotifyingHost (0.5f + 0.4f * std::sin (0.010f * (float) i));
            thr->setValueNotifyingHost (0.5f + 0.4f * std::sin (0.013f * (float) i));
            if (i % 2 == 0)
                freq->setValueNotifyingHost (0.5f + 0.4f * std::sin (0.017f * (float) i));
            record();
            most = juce::jmax (most, um.getNumActionsInCurrentTransaction());
        }
        check (most > 100 && most <= CoherenceProcessor::maxUndoActionsPerStep,
               "automation of three parameters, 50000 values: the undo step never holds more than " + juce::String (most)
                   + " entries (limit " + juce::String (CoherenceProcessor::maxUndoActionsPerStep) + "; v0.6.1 kept them all)");

        const float held = gain->getValue();   // where the automation left it
        panelMove (gain, held > 0.5f ? 0.1f : 0.9f);
        record();
        const float moved = gain->getValue();
        p.undoStep (false);
        check (std::abs (moved - held) > 0.1f && std::abs (gain->getValue() - held) < 1.0e-6f,
               "after it, a panel move is still one step, and UNDO returns it exactly (" + fmt (held, 6) + " to " + fmt (moved, 6) + ", undone " + fmt (gain->getValue(), 6) + ")");
    }

    // The largest step the panel itself makes: MID and SIDE linked and dragged together. Ten seconds of that is about
    // 1000 entries, well inside the limit, and stays one step.
    {
        um.clearUndoHistory();
        const float mid0 = mid->getValue(), side0 = side->getValue();
        um.beginNewTransaction();
        um.beginNewTransaction();   // one from each control, as LINK does
        for (int i = 1; i <= 500; ++i)
        {
            const ScopedWriteSource gui (WriteSource::gui);
            mid->setValueNotifyingHost (mid0 + 0.0005f * (float) i);
            side->setValueNotifyingHost (side0 - 0.0005f * (float) i);
            record();
        }
        const int entries = um.getNumActionsInCurrentTransaction();
        const bool movedBoth = std::abs (mid->getValue() - mid0) > 0.2f && std::abs (side->getValue() - side0) > 0.2f;
        p.undoStep (false);
        check (movedBoth && entries > 500 && std::abs (mid->getValue() - mid0) < 1.0e-6f && std::abs (side->getValue() - side0) < 1.0e-6f,
               "a 10 s drag of two linked controls (" + juce::String (entries) + " entries) is still one step: UNDO returns both exactly (MID " + fmt (mid->getValue(), 6) + ", SIDE " + fmt (side->getValue(), 6) + ", from " + fmt (mid0, 6) + " and " + fmt (side0, 6) + ")");
    }
}

//==============================================================================
// The UC1's Expand button (Gate Expander slot, A/B): a press changes nothing but the slot, until the slots really differ.
void abFirstSwitchChecks()
{
    line ("\n## A/B: the first switch\n");
    const auto settingsDiffer = [] (CoherenceProcessor& p, const std::map<juce::String, float>& before)
    {
        juce::StringArray moved;
        for (auto* q : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q); rp != nullptr && isSavedParameter (*rp) && std::abs (before.at (rp->getParameterID()) - rp->getValue()) > 1.0e-6f)
                moved.add (rp->getParameterID());
        return moved;
    };
    const auto snapshot = [] (CoherenceProcessor& p)
    {
        std::map<juce::String, float> m;
        for (auto* q : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q); rp != nullptr && isSavedParameter (*rp))
                m[rp->getParameterID()] = rp->getValue();
        return m;
    };
    const auto useA = [] (CoherenceProcessor& p)
    {
        p.state().getParameter (id::dynOn)->setValueNotifyingHost (1.0f);
        p.state().getParameter (id::eqMs)->setValueNotifyingHost (1.0f);
        setPlain (p, id::threshold, -31.5f);
        setPlain (p, "hf_gain", 4.5f);
        setPlain (p, id::mix, 62.0f);
    };
    {
        CoherenceProcessor p;
        useA (p);
        const auto a = snapshot (p);
        struct Counter final : juce::AudioProcessorListener
        {
            int writes = 0;
            void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++writes; }
            void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        } counter;
        p.addListener (&counter);
        p.abSelect().setValueNotifyingHost (1.0f);   // as the UC1 does: from outside, through the parameter
        p.syncAbSurface();
        p.removeListener (&counter);
        const auto moved = settingsDiffer (p, a);
        check (p.getAbSlot() == 1 && moved.isEmpty(), "pressing the switch with compressor, M/S and Threshold set on A moves only the slot; settings that moved: " + (moved.isEmpty() ? juce::String ("none") : moved.joinIntoString (", ")));
        check (counter.writes <= 2, "the host hears only the switch itself (" + juce::String (counter.writes) + " parameter writes)");
        p.abSelect().setValueNotifyingHost (0.0f);
        p.syncAbSurface();
        check (p.getAbSlot() == 0 && settingsDiffer (p, a).isEmpty(), "pressing it again goes back to A, and again nothing moves");
    }
    {   // Independent, once they differ.
        CoherenceProcessor p;
        useA (p);
        const auto a = snapshot (p);
        p.selectAbSlot (1);
        p.state().getParameter (id::dynOn)->setValueNotifyingHost (0.0f);
        p.state().getParameter (id::eqMs)->setValueNotifyingHost (0.0f);
        setPlain (p, id::threshold, -12.0f);
        const auto b = snapshot (p);
        p.selectAbSlot (0);
        check (settingsDiffer (p, a).isEmpty(), "once B has its own settings, A comes back exactly");
        p.selectAbSlot (1);
        check (settingsDiffer (p, b).isEmpty(), "and so does B");
    }
    {   // COPY, and the panel, count as settings for B. The first switch keeps working after a save and reload.
        CoherenceProcessor p;
        useA (p);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        CoherenceProcessor r;
        r.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto a = snapshot (r);
        r.selectAbSlot (1);
        check (settingsDiffer (r, a).isEmpty(), "a set saved before B was ever used: the first switch after opening it still moves nothing");
    }
    {   // A set whose other slot has real settings keeps them. Sets from earlier builds have no marker, so they keep what they saved.
        CoherenceProcessor p;
        useA (p);
        p.selectAbSlot (1);
        setPlain (p, id::threshold, -12.0f);
        p.selectAbSlot (0);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        CoherenceProcessor r;
        r.setStateInformation (mb.getData(), (int) mb.getSize());
        r.selectAbSlot (1);
        check (std::abs (r.state().getRawParameterValue (id::threshold)->load() + 12.0f) < 0.01f, "a set whose B has its own settings opens with them");
        CoherenceProcessor untouched;
        useA (untouched);
        juce::MemoryBlock um;
        untouched.getStateInformation (um);
        auto xml = juce::AudioProcessor::getXmlFromBinary (um.getData(), (int) um.getSize());
        xml->removeAttribute ("otherSlotFresh");
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        CoherenceProcessor e;
        e.setStateInformation (old.getData(), (int) old.getSize());
        e.selectAbSlot (1);
        check (std::abs (e.state().getRawParameterValue (id::threshold)->load() + 18.0f) < 0.01f, "a set from an earlier build keeps the B it saved, as it was");
    }
    {   // COPY after a first switch, and COPY before one.
        CoherenceProcessor p;
        useA (p);
        p.copyCurrentToOtherSlot();
        setPlain (p, id::threshold, -40.0f);
        p.selectAbSlot (1);
        check (std::abs (p.state().getRawParameterValue (id::threshold)->load() + 31.5f) < 0.01f, "COPY still copies: B holds A's settings from when COPY was pressed");
    }
}

//==============================================================================
// A/B from the panel and from a control surface (DESIGN.md section 19).
void abSurfaceChecks()
{
    line ("\n## A/B: panel and control surface\n");
    CoherenceProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    auto& ab = p.abSelect();
    const auto press = [&] (float v) { ab.setValueNotifyingHost (v); p.syncAbSurface(); };   // as a surface writes

    {
        const auto& all = p.getParameters();
        const auto idAt = [&] (int i) { return dynamic_cast<juce::RangedAudioParameter*> (all[i])->getParameterID(); };
        check (all.getLast() == &ab && ab.getParameterID() == id::abSelect && ab.getName (64) == "A/B", "the A/B switch is last in the parameter list");
        check (idAt (0) == id::input && idAt (25) == id::mix && idAt (33) == id::output && idAt (36) == id::grMeter && idAt (37) == id::eqOutput
                   && idAt (65) == "focus_hf_q" && all.size() == 67,
               "no existing parameter moved (input 0, Mix 25, Output 33, meter 36, EQ Output 37, last focus 65)");
        check (! ab.isAutomatable() && ab.isMetaParameter() && static_cast<juce::AudioProcessorParameter&> (ab).getNumSteps() == 2 && ab.getCurrentValueAsText() == "A",
               "it has two steps, isn't automatable, is declared as a meta parameter, and starts on A");
    }

    // Every setting, including the Side EQ, EQ Output and the compressor: A and B don't share anything.
    {
        std::vector<juce::RangedAudioParameter*> saved;
        for (auto* q : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q); r != nullptr && isSavedParameter (*r))
                saved.push_back (r);
        std::mt19937 rng (19);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        // The setting each value means: a switch keeps the raw value it was given (0.4), which means off and is saved as 0.
        const auto setting = [] (const juce::RangedAudioParameter* r) { return r->convertTo0to1 (r->convertFrom0to1 (r->getValue())); };
        const auto randomise = [&] { std::vector<float> v; for (auto* r : saved) { r->setValueNotifyingHost (u (rng)); v.push_back (setting (r)); } return v; };
        juce::String worstName;
        const auto worst = [&] (const std::vector<float>& want)
        {
            float w = 0.0f;
            for (size_t i = 0; i < saved.size(); ++i)
                if (const float e = std::abs (setting (saved[i]) - want[i]); e > w)
                {
                    w = e;
                    worstName = saved[i]->getParameterID() + " (" + fmt (want[i], 4) + " came back as " + fmt (setting (saved[i]), 4) + ")";
                }
            return w;
        };
        const auto a = randomise();
        p.toggleAbSlot();
        // B is far from A on every control, so that every switch and choice differs too: 0.6 clears the rounding at 0.5.
        std::vector<float> b;
        for (size_t i = 0; i < saved.size(); ++i)
        {
            saved[i]->setValueNotifyingHost (a[i] >= 0.5f ? juce::jmax (0.0f, a[i] - 0.6f) : juce::jmin (1.0f, a[i] + 0.6f));
            b.push_back (setting (saved[i]));
        }
        int different = 0;
        for (size_t i = 0; i < saved.size(); ++i)
            if (std::abs (a[i] - b[i]) > 1.0e-3f) ++different;
        p.toggleAbSlot();
        const float backToA = worst (a);
        p.toggleAbSlot();
        const float backToB = worst (b);
        check (different == (int) saved.size() && backToA < 1.0e-3f && backToB < 1.0e-3f,
               "A and B differ on all " + juce::String (different) + " of " + juce::String ((int) saved.size()) + " settings, and each keeps its own (largest error " + fmt (juce::jmax (backToA, backToB), 6) + ", normalised" + (worstName.isEmpty() ? juce::String() : ", " + worstName) + ")");
        p.toggleAbSlot();
    }

    // The surface and the panel stay in step.
    check (p.getAbSlot() == 0 && ab.getCurrentValueAsText() == "A", "the switch reads A while A is in use");
    p.toggleAbSlot();
    check (ab.getCurrentValueAsText() == "B", "switching on the panel moves the switch straight away, so the surface's light follows");
    p.toggleAbSlot();
    juce::Thread::sleep (320);   // past the time a host may still pass those two writes back
    press (1.0f);
    check (p.getAbSlot() == 1, "pressing the switch selects B");
    press (0.0f);
    check (p.getAbSlot() == 0, "and again selects A");
    bool every = true;
    for (int i = 1; i <= 8; ++i) { press (i % 2 == 1 ? 1.0f : 0.0f); every = every && p.getAbSlot() == i % 2; }
    check (every, "quick presses on the switch are never lost");

    // A VST3 host passes the plug-in's own writes back in a later block.
    {
        p.toggleAbSlot();
        p.toggleAbSlot();
        press (1.0f);   // the late echo of the write to B
        check (p.getAbSlot() == 0 && ab.getCurrentValueAsText() == "A", "a late echo after toggling on the panel doesn't switch the slot, and the switch comes back");
        juce::Thread::sleep (320);
        press (1.0f);
        check (p.getAbSlot() == 1, "a real press after that switches");
        press (0.0f);
    }

    // The panel's buttons, and both directions at once.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* ed = dynamic_cast<CoherenceEditor*> (editor.get());
        auto* a = ed->findAbButton (0);
        auto* b = ed->findAbButton (1);
        const auto is = [&] (int slot) { return p.getAbSlot() == slot && a->isLit() == (slot == 0) && b->isLit() == (slot == 1) && ab.getCurrentValueAsText() == (slot == 0 ? "A" : "B"); };
        check (is (0), "on opening, A is lit");
        a->trigger();
        check (is (1), "clicking A while A is in use switches to B, and B is lit");
        b->trigger();
        check (is (0), "clicking B while B is in use switches to A, and A is lit");
        b->trigger();
        check (is (1), "clicking B while A is in use switches to B");
        a->trigger();
        check (is (0), "clicking A while B is in use switches to A");
        juce::Thread::sleep (320);
        press (1.0f);
        check (is (1), "pressing the switch lights B on the panel");
        a->trigger();
        check (is (0), "clicking the panel after that switches back, and the switch follows");
        juce::Thread::sleep (320);

        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 200; ++i)
            p.toggleAbSlot();
        const double ms = (juce::Time::getMillisecondCounterHiRes() - start) / 200.0;
        line ("- one switch takes " + fmt (ms, 3) + " ms on the message thread, with the panel open");
        check (ms < 25.0, "one A/B switch takes under 25 ms (" + fmt (ms, 3) + " ms)");
        juce::Thread::sleep (320);
    }

    // Recall and old sets.
    {
        auto& s = p.state();
        s.getParameter (id::threshold)->setValueNotifyingHost (0.25f);
        p.toggleAbSlot();
        s.getParameter (id::threshold)->setValueNotifyingHost (0.75f);
        const float inB = s.getParameter (id::threshold)->getValue();
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        check (! juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize())->toString().contains (id::abSelect), "the switch isn't saved with the set; the set saves the selected slot as it always has");

        CoherenceProcessor back;
        back.setStateInformation (mb.getData(), (int) mb.getSize());
        back.syncAbSurface();
        check (back.getAbSlot() == 1 && back.abSelect().getCurrentValueAsText() == "B" && std::abs (back.state().getParameter (id::threshold)->getValue() - inB) < 1.0e-3f,
               "a set saved on B opens on B, and the switch reads B");
        back.toggleAbSlot();
        check (std::abs (back.state().getParameter (id::threshold)->getValue() - 0.25f) < 2.0e-3f && back.abSelect().getCurrentValueAsText() == "A", "and A is still there");

        // A v0.5.1 set: PARAM children, abSlot and OTHER_SLOT, and no switch.
        juce::ValueTree old ("COHERENCE");
        old.setProperty ("abSlot", 1, nullptr);
        juce::ValueTree c ("PARAM");
        c.setProperty ("id", id::threshold, nullptr);
        c.setProperty ("value", -33.0f, nullptr);
        old.appendChild (c, nullptr);
        juce::MemoryBlock oldBlock;
        juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldBlock);
        CoherenceProcessor fromOld;
        fromOld.setStateInformation (oldBlock.getData(), (int) oldBlock.getSize());
        fromOld.syncAbSurface();
        check (fromOld.getAbSlot() == 1 && fromOld.abSelect().getCurrentValueAsText() == "B", "an older set without the switch opens on the slot it was saved on");
    }
}

//==============================================================================
// Themes (DESIGN.md section 20). A theme is colours and nothing else.
namespace
{
void setApprovedPreviewValues (CoherenceProcessor& p)
{
    auto& s = p.state();
    const auto set = [&s] (const juce::String& pid, float v) { auto* q = s.getParameter (pid); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    for (auto [pid, v] : std::initializer_list<std::pair<juce::String, float>> {
             { "hf_gain", 1.5f }, { "hf_freq", 12000.0f }, { "hf_q", 0.707f }, { "hf_type", 1.0f }, { "hmf_gain", -2.0f }, { "hmf_freq", 3150.0f }, { "hmf_q", 1.8f },
             { "lmf_gain", 1.0f }, { "lmf_freq", 240.0f }, { "lmf_q", 0.8f }, { "lf_gain", 3.0f }, { "lf_freq", 60.0f }, { "lf_q", 0.707f }, { "lf_type", 1.0f },
             { id::input, 1.0f }, { id::mid, -0.5f }, { id::side, 1.5f }, { id::output, -1.5f }, { id::threshold, -22.0f }, { id::ratio, 3.0f }, { id::attack, 12.0f },
             { id::release, 180.0f }, { id::knee, 0.4f }, { id::makeup, 2.0f }, { id::scHpf, 90.0f }, { id::mix, 85.0f }, { id::dynOn, 1.0f }, { id::eqMode, 1.0f } })
        set (pid, v);
}

void collectBounds (juce::Component& c, juce::Array<juce::Rectangle<int>>& out)
{
    for (auto* child : c.getChildren())
    {
        out.add (child->getBounds());
        collectBounds (*child, out);
    }
}

juce::Image renderEditor (juce::AudioProcessorEditor& e)
{
    juce::Image im (juce::Image::ARGB, e.getWidth(), e.getHeight(), true);
    juce::Graphics g (im);
    e.paintEntireComponent (g, true);
    return im;
}

int pixelsDifferent (const juce::Image& a, const juce::Image& b)
{
    int n = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                ++n;
    return n;
}
} // namespace

void themeChecks()
{
    line ("\n## Themes\n");
    {   // Classic is the approved design (mac-results/gui-preview): its colours don't change.
        const auto& c = theme::themeAt (0);
        const auto same = [] (juce::Colour a, juce::uint32 b) { return a.getARGB() == b; };
        check (same (c.panelTop, 0xff2f3239) && same (c.panel, 0xff292a2f) && same (c.header, 0xff25262c) && same (c.meterColumn, 0xff18191b) && same (c.block, 0xff2e3037)
                   && same (c.stripTop, 0xffb4b5b6) && same (c.stripBottom, 0xff939496) && same (c.ink, 0xffa1a2a6) && same (c.rule, 0xff808186) && same (c.capHf, 0xff7c1b1a)
                   && same (c.capHmf, 0xff1c422a) && same (c.capLmf, 0xff2c6793) && same (c.capLf, 0xff4f3531) && same (c.meterLitTop, 0xffe06f18) && same (c.lcdLit, 0xff39bd63),
               "the Classic palette is still the approved one");
        bool shared = true;
        for (int i = 1; i < theme::numThemes(); ++i)
        {
            const auto& o = theme::themeAt (i);
            shared = shared && o.capHf == c.capHf && o.capHmf == c.capHmf && o.capLmf == c.capLmf && o.capLf == c.capLf && o.meterLitTop == c.meterLitTop && o.meterLitBottom == c.meterLitBottom
                     && o.meterUnlit == c.meterUnlit && o.lcdLit == c.lcdLit && o.scaleHot == c.scaleHot && o.scaleCool == c.scaleCool;
        }
        check (shared, "every theme keeps the EQ band colours, orange meters and green displays");
    }
    const auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("coherence-theme-test", ".settings");
    UserSettings::setFileForTesting (tempFile);
    {
        juce::StringArray ids;
        for (int i = 0; i < theme::numThemes(); ++i)
            ids.addIfNotAlreadyThere (theme::themeAt (i).id);
        check (theme::numThemes() == 3 && ids.size() == 3 && ids[0] == "classic" && juce::String (theme::themeAt (1).name) == "Warm Graphite" && juce::String (theme::themeAt (2).name) == "Champagne",
               "three themes, with distinct saved names, and Classic first");

        juce::SharedResourcePointer<UserSettings> settings;
        check (settings->themeId() == "classic", "a new user starts on Classic");

        // The same settings through a fresh processor, so that nothing is left in the audio engine from a previous render.
        const auto freshAudio = []
        {
            CoherenceProcessor q;
            q.setPlayConfigDetails (2, 2, 48000.0, 512);
            q.prepareToPlay (48000.0, 512);
            setApprovedPreviewValues (q);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            juce::MemoryBlock out;
            for (int b = 0; b < 8; ++b)
            {
                for (int i = 0; i < 512; ++i) { const float x = 0.3f * std::sin ((float) (b * 512 + i) * 0.05f); buf.setSample (0, i, x); buf.setSample (1, i, 0.7f * x); }
                q.processBlock (buf, midi);
                out.append (buf.getReadPointer (0), 512 * sizeof (float));
                out.append (buf.getReadPointer (1), 512 * sizeof (float));
            }
            return out;
        };

        CoherenceProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        setApprovedPreviewValues (p);
        {   // One block first, so that the reported latency has settled: the panel prints it.
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            buf.clear();
            p.processBlock (buf, midi);
        }
        std::unique_ptr<juce::AudioProcessorEditor> e1 (p.createEditor()), e2 (p.createEditor());
        auto* ed = dynamic_cast<CoherenceEditor*> (e1.get());

        const auto stateNow = [&p] { juce::MemoryBlock m; p.getStateInformation (m); return m; };
        const auto paramsNow = [&p] { juce::Array<float> v; for (auto* q : p.getParameters()) v.add (q->getValue()); return v; };
        // The panel's own layout: the window's resize corner moves with the window.
        const auto boundsNow = [&ed] { juce::Array<juce::Rectangle<int>> b; collectBounds (*ed->getChildComponent (0), b); return b; };

        const auto classicImage = renderEditor (*ed).createCopy();
        const auto state0 = stateNow();
        const auto params0 = paramsNow();
        const auto bounds0 = boundsNow();
        const auto audio0 = freshAudio();

        bool sameLayout = true, sameParams = true, sameState = true, sameAudio = true, follows = true, differs = true;
        for (int i = 1; i < theme::numThemes(); ++i)
        {
            settings->setThemeId (theme::themeAt (i).id);
            settings->dispatchPendingMessages();
            follows = follows && juce::String (theme::current().id) == theme::themeAt (i).id;
            const auto image = renderEditor (*ed);
            differs = differs && pixelsDifferent (classicImage, image) > 10000;
            sameLayout = sameLayout && boundsNow() == bounds0;
            sameParams = sameParams && paramsNow() == params0;
            sameState = sameState && stateNow() == state0;
            sameAudio = sameAudio && freshAudio() == audio0;
        }
        check (follows, "changing the theme takes effect at once, with no restart, in every open instance");
        check (differs, "each theme really looks different");
        check (sameLayout, "every control is in exactly the same place in every theme");
        check (sameParams && sameState, "a theme changes no parameter, and nothing in the saved set");
        check (sameAudio, "a theme changes no audio (the output is bit-identical)");

        settings->setThemeId ("classic");
        settings->dispatchPendingMessages();
        const int classicAgain = pixelsDifferent (classicImage, renderEditor (*ed));
        check (classicAgain == 0, "returning to Classic restores it pixel for pixel (" + juce::String (classicAgain) + " pixels differ)");

        bool resizes = true;
        for (int i = 0; i < theme::numThemes(); ++i)
        {
            settings->setThemeId (theme::themeAt (i).id);
            settings->dispatchPendingMessages();
            for (float sc : { 0.6f, 0.8f, 1.0f, 1.25f, 1.6f, 2.0f })
            {
                ed->setScale (sc);
                resizes = resizes && ed->getWidth() == juce::roundToInt ((float) CoherenceEditor::baseWidth * sc) && ed->getHeight() == juce::roundToInt ((float) CoherenceEditor::baseHeight * sc)
                          && boundsNow() == bounds0;
            }
            ed->setScale (1.0f);
        }
        check (resizes, "window resizing behaves the same in every theme, at 6 sizes");

        settings->setThemeId ("champagne");
        settings->dispatchPendingMessages();
        {
            UserSettings reopened;
            check (reopened.themeId() == "champagne", "the choice is kept in the user's settings, across sessions");
        }
        {
            juce::MemoryBlock m = state0;
            CoherenceProcessor other;
            other.setStateInformation (m.getData(), (int) m.getSize());
            check (settings->themeId() == "champagne", "loading a set or preset doesn't change the theme");
        }
        theme::setCurrent ("a-theme-from-the-future");
        check (juce::String (theme::current().id) == "classic", "an unknown saved theme falls back to Classic");
        settings->setThemeId ("classic");
        settings->dispatchPendingMessages();
    }
    tempFile.deleteFile();
    UserSettings::setFileForTesting ({});
}

// Every control has to be reachable: their components overlap, because a knob's bounds hold its printed scale.
void panelReachChecks()
{
    line ("\n## Panel\n");
    CoherenceProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* ed = dynamic_cast<CoherenceEditor*> (editor.get());
    ed->setVisible (true);

    juce::StringArray missed;
    int reached = 0;
    for (const juce::String pid : { "hf_gain", "hf_freq", "hf_q", "hmf_gain", "hmf_freq", "hmf_q", "lmf_gain", "lmf_freq", "lmf_q", "lf_gain", "lf_freq", "lf_q",
                                    id::input, id::balance, id::mid, id::side, id::ratio, id::threshold, id::attack, id::release, id::knee, id::scHpf, id::makeup })
    {
        auto* k = ed->findKnob (pid);
        if (k == nullptr) { missed.add (pid + " (not found)"); continue; }
        // The middle of the knob, then the top and bottom of its cap.
        for (const float dy : { 0.0f, -11.0f, 11.0f })
        {
            const auto local = juce::Point<float> (gui::Knob::centreX, gui::Knob::centreY + dy).roundToInt();
            const auto at = ed->getLocalPoint (k, local);
            if (ed->getComponentAt (at) != k) { missed.add (pid); break; }
            ++reached;
        }
    }
    check (missed.isEmpty(), "every knob can be clicked, even where the printed scales overlap (" + juce::String (reached) + " points" + (missed.isEmpty() ? juce::String() : "; missed: " + missed.joinIntoString (", ")) + ")");

    // Everything else that takes the mouse: keys, readouts, the fader, the meters, the order screen.
    {
        int controls = 0;
        juce::StringArray blocked;
        auto* panel = ed->getChildComponent (0);
        for (auto* c : panel->getChildren())
        {
            bool self = false, children = false;
            c->getInterceptsMouseClicks (self, children);
            if (! self || ! c->isVisible() || dynamic_cast<gui::Knob*> (c) != nullptr)
                continue;
            ++controls;
            const auto at = ed->getLocalPoint (c, c->getLocalBounds().getCentre());
            if (ed->getComponentAt (at) != c)
                blocked.add (c->getBounds().toString());
        }
        check (controls >= 18 && blocked.isEmpty(), "all " + juce::String (controls) + " other controls can be clicked" + (blocked.isEmpty() ? juce::String() : "; blocked at " + blocked.joinIntoString (" / ")));
    }

    // The knob's own text, the readouts, and stepping through them.
    {
        auto* eqOut = ed->findKnob (id::eqOutput);
        auto* mix = ed->findKnob (id::mix);
        check (eqOut != nullptr && mix != nullptr, "EQ Output and Mix are on the panel as readouts");
        auto& q = eqOut->parameter();
        q.setValueNotifyingHost (q.convertTo0to1 (0.0f));
        eqOut->setValueSteps (0.5f, 0.01f);
        eqOut->stepBy (3, false);
        check (std::abs (q.convertFrom0to1 (q.getValue()) - 1.5f) < 1e-3f && eqOut->currentText().contains ("1.5"), "the EQ Output readout steps like a knob, and reads " + eqOut->currentText());
        q.setValueNotifyingHost (q.convertTo0to1 (0.0f));
    }
    check (theme::lcdString (juce::String::fromUTF8 ("\xe2\x88\x92" "12.5 dB")) == "-12.5 DB" && theme::lcdString (juce::String::fromUTF8 ("50 \xc2\xb5s")) == "50 US", "the dot-matrix readouts can show every value's text");
}

// The approved design's control values, for measuring the panel against the approved image.
void approvedSnapshot (const juce::File& out, const juce::String& themeId, float windowScale, bool sideSelected)
{
    const auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("coherence-theme-snapshot", ".settings");
    UserSettings::setFileForTesting (tempFile);
    {
        juce::SharedResourcePointer<UserSettings> settings;
        settings->setThemeId (themeId);
        CoherenceProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        setApprovedPreviewValues (p);
        if (sideSelected)
            for (const auto* id : { "eq_ms", "eq_channel" })
                p.state().getParameter (id)->setValueNotifyingHost (1.0f);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        buf.clear();
        p.processBlock (buf, midi);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* ed = dynamic_cast<CoherenceEditor*> (editor.get());
        ed->setScale (windowScale);   // 1.6 is 1440 pixels wide, as the approved image
        ed->hoverChanged (p.state().getParameter ("hf_gain"), {});
        ed->tickMetersForPreview (2);
        ed->setVisible (true);
        const auto image = renderEditor (*ed);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    }
    tempFile.deleteFile();
    UserSettings::setFileForTesting ({});
}

void weissModeChecks()
{
    constexpr double pi = juce::MathConstants<double>::pi;
    line ("\n## Weiss mode (the Weiss EQ MP model at 2x)\n");
    const int fs = 48000;

    // 1. Against Weiss EQ MP itself: the measured magnitude and phase at 11 frequencies for 9 settings.
    {
        double worstMag = 0.0, worstPh = 0.0;
        juce::String worstName;
        for (const auto& w : weissGolden)
        {
            Engine e;
            e.prepare (fs, 512, 2);
            auto st = neutral();
            st.weissEq = true;
            st.bands[(size_t) bandSlotFor (w.shape)] = BandSettings { w.shape, w.f0, w.g, w.q };
            const int lat = weissLatencySamples, pre = 4800, len = pre + fs * 3 / 2;
            std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) (len + lat + pre), 0.0f));
            x[0][(size_t) pre] = x[1][(size_t) pre] = 1.0f;
            const auto y = run (e, st, x);
            for (int k = 0; k < 11; ++k)
            {
                std::complex<double> H {};
                for (int n = 0; n < len; ++n)   // the impulse response from 0.1 s before the impulse, delay compensated
                    H += (double) y[0][(size_t) (n + lat)] * std::polar (1.0, -2.0 * pi * weissGoldenFreqs[k] * (double) (n - pre) / fs);
                const double dm = std::abs (20.0 * std::log10 (std::abs (H)) - w.mag[k]);
                const double dp = std::abs (std::remainder (std::arg (H) * 180.0 / pi - w.ph[k], 360.0));
                if (dm > worstMag) { worstMag = dm; worstName = w.name; }
                worstPh = std::max (worstPh, dp);
            }
        }
        check (worstMag < 0.005 && worstPh < 0.05,
               "Weiss mode against Weiss EQ MP (9 settings: bells at 1k/10k/15k, high shelves 3k/10k/15k with Q 0.71-2, low shelves Q 0.5-4 at -6..+12 dB; "
               "11 frequencies to 20 kHz): worst magnitude " + fmt (worstMag, 4) + " dB (" + worstName + "), worst phase " + fmt (worstPh, 3) + " degrees");
    }

    // 2. With the EQ flat, Weiss mode is an exact delay of 15 samples (the half-band's centre tap is 1).
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto st = neutral();
        st.weissEq = true;
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> u (-0.9f, 0.9f);
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) fs));
        for (auto& c : x) for (auto& v : c) v = u (rng);
        const auto y = run (e, st, x, 77);
        bool exact = true;
        for (int c = 0; c < 2; ++c)
            for (size_t n = (size_t) weissLatencySamples; n < x[0].size(); ++n)
                exact = exact && y[(size_t) c][n] == x[(size_t) c][n - (size_t) weissLatencySamples];
        auto stOff = st; stOff.eqOn = false;
        Engine e2; e2.prepare (fs, 512, 2);
        const auto y2 = run (e2, stOff, x, 64);
        bool exactOff = true;
        for (size_t n = (size_t) weissLatencySamples; n < x[0].size(); ++n)
            exactOff = exactOff && y2[0][n] == x[0][n - (size_t) weissLatencySamples];
        check (exact && exactOff && e.latencySamples() == weissLatencySamples,
               "Weiss mode with the EQ flat, and with EQ IN off, is an exact 15-sample delay (bit-identical): the 2x resampling adds nothing else");
    }

    // 2b. Other sample rates (no Weiss renders exist there yet): exact delay, and a 1 kHz +6 dB bell exact at its centre.
    {
        bool ok = true;
        juce::String detail;
        for (int rate : { 44100, 88200, 96000, 192000 })
        {
            Engine e;
            e.prepare (rate, 512, 2);
            auto st = neutral();
            st.weissEq = true;
            st.bands[2] = BandSettings { Shape::bell, 1000.0, 6.0, 1.0 };
            std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) rate * 2));
            for (size_t n = 0; n < x[0].size(); ++n) x[0][n] = x[1][n] = (float) std::sin (2.0 * pi * 1000.0 * (double) n / rate);
            const auto y = run (e, st, x);
            double peak = 0.0;
            for (size_t n = x[0].size() / 2; n < x[0].size(); ++n) peak = std::max (peak, (double) std::abs (y[0][n]));
            const double g = 20.0 * std::log10 (peak);
            ok = ok && std::abs (g - 6.0) < 0.01;
            detail << juce::String (rate / 1000.0, 1) << " kHz " << fmt (g, 3) << " dB; ";
        }
        check (ok, "Weiss mode at 44.1-192 kHz: a 1 kHz +6 dB bell measures +6.00 dB at its centre (" + detail.trimEnd().dropLastCharacters (1)
                       + "). Only 48 kHz is verified against Weiss itself");
    }

    // 2c. Extremes: every shape at +-15 dB, Q 0.3 and 10, 16 Hz and 20 kHz, at 44.1-192 kHz, stays finite and bounded.
    {
        bool ok = true;
        for (int rate : { 44100, 48000, 96000, 192000 })
            for (double f : { 16.0, 20000.0 })
                for (double q : { 0.3, 10.0 })
                    for (double g : { -15.0, 15.0 })
                    {
                        Engine e;
                        e.prepare (rate, 512, 2);
                        auto st = neutral();
                        st.weissEq = true;
                        st.bands[0] = BandSettings { Shape::lowShelf, f, g, q };
                        st.bands[1] = BandSettings { Shape::bell, f, g, q };
                        st.bands[2] = BandSettings { Shape::lowCut, f, 0.0, q };
                        st.bands[3] = BandSettings { Shape::highShelf, f, g, q };
                        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) rate * 2, 0.0f));
                        x[0][100] = x[1][100] = 1.0f;
                        const auto y = run (e, st, x);
                        // Bounded (the worst case, four resonant bands stacked at 20 kHz, peaks at +61 dB) and decaying:
                        // the last 0.2 s is below the 0.2 s before it (four Q 10 bands stacked at 16 Hz ring for seconds).
                        float mx = 0.0f, tail = 0.0f, before = 0.0f;
                        const size_t N = y[0].size(), w = (size_t) rate / 5;
                        for (size_t n = 0; n < N; ++n)
                        {
                            const float v = y[0][n];
                            ok = ok && std::isfinite (v);
                            mx = std::max (mx, std::abs (v));
                            if (n >= N - w) tail = std::max (tail, std::abs (v));
                            else if (n >= N - 2 * w) before = std::max (before, std::abs (v));
                        }
                        ok = ok && mx < 1.0e5f && (tail < 0.7f * before || tail < 1.0e-6f);
                    }
        check (ok, "Weiss mode extremes (shelves, bell and cut at +-15 dB, Q 0.3 and 10, 16 Hz and 20 kHz, 44.1-192 kHz): finite and decaying");
    }

    // 3. The plug-in reports the latency, and the measured delay matches it.
    {
        CoherenceProcessor p;
        auto* mode = p.state().getParameter (id::eqMode);
        mode->setValueNotifyingHost (mode->convertTo0to1 (1.0f));
        p.prepareToPlay (fs, 256);
        juce::AudioBuffer<float> b (2, 256);
        b.clear();
        b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        juce::MidiBuffer midi;
        p.processBlock (b, midi);
        int peak = 0;
        for (int i = 0; i < 256; ++i) if (std::abs (b.getSample (0, i)) > std::abs (b.getSample (0, peak))) peak = i;
        const int latW = p.getLatencySamples();
        mode->setValueNotifyingHost (mode->convertTo0to1 (0.0f));
        p.processBlock (b, midi);
        const int latN = p.getLatencySamples();
        check (latW == weissLatencySamples && peak == weissLatencySamples && latN == 0,
               "the plug-in reports 15 samples of latency in Weiss mode (impulse measured at sample " + juce::String (peak) + ") and 0 in COHERENCE mode");
    }

    // 4. Switching mode while audio plays: a 5 ms fade out and in, no click.
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto st = neutral();
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) fs / 2));
        for (size_t n = 0; n < x[0].size(); ++n) x[0][n] = x[1][n] = 0.5f * (float) std::sin (2.0 * pi * 1000.0 * (double) n / fs);
        MeterData m;
        auto y = x;
        double worstStep = 0.0;
        const double sineStep = 0.5 * 2.0 * pi * 1000.0 / fs;
        for (int start = 0, blk = 0; start < (int) x[0].size(); start += 256, ++blk)
        {
            st.weissEq = (blk / 20) % 2 == 1;   // toggles every 20 blocks
            float* io[2] = { y[0].data() + start, y[1].data() + start };
            e.process (io, 2, std::min (256, (int) x[0].size() - start), st, m);
        }
        for (size_t n = 1; n < y[0].size(); ++n) worstStep = std::max (worstStep, (double) std::abs (y[0][n] - y[0][n - 1]));
        check (worstStep <= sineStep * 1.05, "switching between COHERENCE and Weiss mode while a sine plays is click-free (5 ms fade out and in; largest step "
                                              + fmt (worstStep / sineStep, 3) + "x the sine's own)");
    }

    // 5. Bypass in Weiss mode stays in time: fully bypassed is the input delayed by the latency.
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto st = neutral();
        st.weissEq = true;
        st.bypass = true;
        st.bands[2] = BandSettings { Shape::bell, 1000.0, 12.0, 1.0 };
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) fs / 4));
        std::mt19937 rng (9);
        std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        for (auto& c : x) for (auto& v : c) v = u (rng);
        const auto y = run (e, st, x);
        double worst = 0.0;
        for (size_t n = (size_t) weissLatencySamples; n < x[0].size(); ++n)
            worst = std::max (worst, (double) std::abs (y[0][n] - x[0][n - (size_t) weissLatencySamples]));
        check (worst == 0.0, "bypass in Weiss mode: the output is the input delayed by the same 15 samples (bit-identical), so bypassing never jumps in time");
    }

    // 6. Images and aliasing: 19 kHz at -6 dBFS through a +12 dB high shelf at 10 kHz; everything that isn't 19 kHz.
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto st = neutral();
        st.weissEq = true;
        st.bands[3] = BandSettings { Shape::highShelf, 10000.0, 12.0, 0.71 };
        const int n = fs;
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) n));
        for (int i = 0; i < n; ++i) x[0][(size_t) i] = x[1][(size_t) i] = 0.5f * (float) std::sin (2.0 * pi * 19000.0 * i / fs);
        const auto y = run (e, st, x);
        // Remove the 19 kHz tone by least squares over the last half second, then measure what is left.
        const int a0 = n / 2;
        double cc = 0, cs = 0, ss = 0, yc = 0, ys = 0;
        for (int i = a0; i < n; ++i)
        {
            const double c = std::cos (2.0 * pi * 19000.0 * i / fs), s = std::sin (2.0 * pi * 19000.0 * i / fs), v = y[0][(size_t) i];
            cc += c * c; ss += s * s; cs += c * s; yc += v * c; ys += v * s;
        }
        const double det = cc * ss - cs * cs, A = (yc * ss - ys * cs) / det, B = (ys * cc - yc * cs) / det;
        double res = 0, sig = 0;
        for (int i = a0; i < n; ++i)
        {
            const double t = A * std::cos (2.0 * pi * 19000.0 * i / fs) + B * std::sin (2.0 * pi * 19000.0 * i / fs);
            res += (y[0][(size_t) i] - t) * (y[0][(size_t) i] - t);
            sig += t * t;
        }
        const double spur = 10.0 * std::log10 (res / sig + 1e-30);
        check (spur < -90.0, "the 2x chain is time-invariant (interpolation, then decimation of the same phase): a 19 kHz tone through a +12 dB shelf "
                             "produces no other frequency (everything else at " + fmt (spur, 1) + " dB, the float floor)");
    }

    // 7. State: the mode is saved with the set; a set from before v0.2.5 opens in COHERENCE mode.
    {
        CoherenceProcessor p;
        auto* mode = p.state().getParameter (id::eqMode);
        mode->setValueNotifyingHost (1.0f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        CoherenceProcessor r;
        r.setStateInformation (mb.getData(), (int) mb.getSize());
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        for (auto* c : xml->getChildIterator())
            if (c->getStringAttribute ("id") == id::eqMode) { xml->removeChildElement (c, true); break; }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        CoherenceProcessor o;
        o.state().getParameter (id::eqMode)->setValueNotifyingHost (1.0f);
        o.setStateInformation (old.getData(), (int) old.getSize());
        check (r.state().getParameter (id::eqMode)->getValue() > 0.5f && o.state().getParameter (id::eqMode)->getValue() < 0.5f,
               "the EQ mode is saved with the set; a set saved before v0.2.5 opens in COHERENCE (zero-latency) mode");
    }

    // 8. Cost.
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto st = neutral();
        st.bands[0] = BandSettings { Shape::lowShelf, 100, 3, 0.71 };
        st.bands[1] = BandSettings { Shape::bell, 400, -2, 1.0 };
        st.bands[2] = BandSettings { Shape::bell, 3000, 2, 1.4 };
        st.bands[3] = BandSettings { Shape::highShelf, 10000, 1.5, 0.71 };
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) fs * 4, 0.1f));
        auto time = [&] (bool weissOn)
        {
            st.weissEq = weissOn;
            Engine en; en.prepare (fs, 512, 2);
            const auto t0 = std::chrono::steady_clock::now();
            run (en, st, x, 512);
            return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 4.0 * 100.0;
        };
        const double tn = time (false), tw = time (true);
        line ("  INFO  CPU, whole plug-in, 4 bands active, 48 kHz stereo: COHERENCE mode " + fmt (tn, 2) + " %, Weiss mode " + fmt (tw, 2) + " % of one core");
    }
}

//==============================================================================
// v0.2.4: the gain-reduction meter parameter for control surfaces (SSL UC1 via 360 Link).
void controlSurfaceMeter()
{
    line ("\n## Gain-reduction meter for control surfaces (SSL UC1 via 360 Link)\n");
    CoherenceProcessor p;
    auto& m = p.gainReductionMeter();
    check (m.getCategory() == juce::AudioProcessorParameter::compressorLimiterGainReductionMeter && ! m.isAutomatable() && isMeterParameter (m)
               && m.getName (64) == "Gain Reduction",
           "\"Gain Reduction\" is a gain-reduction meter parameter and not automatable: JUCE marks it read-only in VST3 (kIsReadOnly) and as a meter in AU");

    double worstMap = 0.0;
    for (float db : { -0.1f, -0.5f, -1.0f, -3.0f, -6.0f, -12.0f, -20.0f, -40.0f })
        worstMap = std::max (worstMap, (double) std::abs (20.0f * std::log10 (1.0f - gainReductionToMeterValue (db)) - db));
    check (worstMap < 1e-3 && gainReductionToMeterValue (0.0f) == 0.0f,
           "mapping follows SSL's 360 Link convention: 0 = no reduction, value = 1 - 10^(GR/20), and Link's 20 log10 (1 - value) gives back the reduction "
           "from 0.1 to 40 dB (worst " + fmt (worstMap * 1000.0, 2) + " mdB)");

    // Live: -6 dBFS sine into threshold -30 dB, 4:1 -> 18 dB of reduction on the static curve.
    auto set = [&] (const juce::String& pid, float v) { auto* q = p.state().getParameter (pid); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    set (id::dynOn, 1.0f); set (id::threshold, -30.0f); set (id::ratio, 4.0f); set (id::attack, 1.0f); set (id::release, 20.0f);
    set (id::autoRelease, 0.0f); set (id::autoMakeup, 0.0f); set (id::mix, 100.0f);
    struct Counter final : juce::AudioProcessorParameter::Listener
    {
        int n = 0;
        void parameterValueChanged (int, float) override { ++n; }
        void parameterGestureChanged (int, bool) override {}
    } counter;
    m.addListener (&counter);
    const double fs = 48000.0;
    const int block = 512;
    p.prepareToPlay (fs, block);
    juce::MidiBuffer midi;
    double phase = 0.0;
    auto runFor = [&] (double seconds, float amp)
    {
        for (int done = 0; done < (int) (seconds * fs); done += block)
        {
            juce::AudioBuffer<float> b (2, block);
            for (int i = 0; i < block; ++i)
            {
                const float x = amp * (std::sin (phase) >= 0.0 ? 1.0f : -1.0f);   // square: the detector reads its level exactly
                phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / fs;
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
            p.processBlock (b, midi);
        }
    };
    const float amp = juce::Decibels::decibelsToGain (-6.0f);
    runFor (1.0, amp);
    const int before = counter.n;
    runFor (1.0, amp);
    const int steady = counter.n - before;
    const float held = meterValueToGainReduction (m.get());
    const auto text = m.getCurrentValueAsText();
    const float expected = (float) Compressor::staticGainDb (-6.0, -30.0, 0.75, kneeWidthDb (0.4));
    check (std::abs (held - expected) < 0.05f && steady <= 2,
           "live: " + fmt (-expected, 2) + " dB of reduction on the static curve reads " + fmt (held, 2) + " dB on the meter parameter (\"" + text + "\"); a steady reduction sends "
               + juce::String (steady) + " updates per second (only changes are sent)");

    // Release: the meter follows the compressor's own release (20 ms setting: tau about 12 ms), not added ballistics.
    runFor (2.0 * 512.0 / fs, 0.0f);
    const float after1 = meterValueToGainReduction (m.get());
    runFor (0.3, 0.0f);
    const float after300 = meterValueToGainReduction (m.get());
    runFor (1.0, 0.0f);
    const float after1300 = meterValueToGainReduction (m.get());
    const int idleBefore = counter.n;
    runFor (1.0, 0.0f);
    check (after1 > held + 0.5f && after1 < -1.0f && after300 > -0.5f && m.get() == 0.0f && after1300 == 0.0f && counter.n == idleBefore,
           "the meter is the actual reduction: instant attack, then the compressor's own release (" + fmt (after1, 1) + " dB 21 ms after the signal stops, "
               + fmt (after300, 2) + " dB after 0.3 s), back to exactly 0 and silent when idle");
    m.removeListener (&counter);

    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    check (xml != nullptr && ! xml->toString().contains (id::grMeter), "the meter is not saved with the set and not part of A/B (no gr_meter in the state)");
}

//==============================================================================
// v0.2.2: Mid/Side checks beyond the level maths ("Architecture"): independence, automation, LINK.
// Note: the Mid/Side stage is level only (MID, SIDE, LINK, BALANCE). There is no Mid/Side EQ (separate
// frequency / gain / Q for Mid and Side); the EQ processes L and R identically.
void midSideChecks()
{
    line ("\n## Mid/Side: independence, automation, LINK\n");
    const double fs = 48000.0;
    const int n = (int) fs;
    {   // A pure Side signal ignores MID, a pure Mid signal ignores SIDE: exactly.
        std::vector<std::vector<float>> sideOnly (2, std::vector<float> ((size_t) n)), midOnly (2, std::vector<float> ((size_t) n));
        auto x = noise (n, 0.2f, 41);
        for (int i = 0; i < n; ++i)
        {
            sideOnly[0][(size_t) i] = x[0][(size_t) i]; sideOnly[1][(size_t) i] = -x[0][(size_t) i];
            midOnly[0][(size_t) i] = x[0][(size_t) i];  midOnly[1][(size_t) i] = x[0][(size_t) i];
        }
        auto a = neutral(); a.midDb = -12.0;
        auto b = neutral(); b.sideDb = -12.0;
        Engine e1, e2; e1.prepare (fs, 512, 2); e2.prepare (fs, 512, 2);
        const auto ya = run (e1, a, sideOnly), yb = run (e2, b, midOnly);
        const double d = std::max ({ maxAbsDiff (ya[0], sideOnly[0]), maxAbsDiff (ya[1], sideOnly[1]), maxAbsDiff (yb[0], midOnly[0]), maxAbsDiff (yb[1], midOnly[1]) });
        check (d < 1e-7, "MID and SIDE are independent: MID -12 dB leaves a pure Side signal unchanged, SIDE -12 dB leaves a pure Mid signal unchanged (max difference "
                             + juce::String (d, 9) + ")");
    }
    {   // Host automation of MID and SIDE every block: smooth (no zipper), no leakage into the other component.
        Engine e; e.prepare (fs, 512, 2);
        MeterData m;
        auto x = sine (n * 2, fs, 1000.0, 0.3f, 0.1f);
        auto y = x;
        for (int start = 0, blk = 0; start < n * 2; start += 512, ++blk)
        {
            auto st = neutral();
            st.midDb = (blk & 1) ? -6.0 : 0.0;   // a square wave of automation, every block
            st.sideDb = (blk & 2) ? 4.0 : 0.0;
            float* ptr[2] = { y[0].data() + start, y[1].data() + start };
            e.process (ptr, 2, std::min (512, n * 2 - start), st, m);
        }
        // Energy above 4 kHz relative to the signal (zipper noise from the gain changes): four cascaded
        // second-order high-passes at 4 kHz (about 96 dB down at 1 kHz), on the same block-automated output
        // and, for reference, on the unprocessed sine.
        const auto hpq = designBilinear (Shape::lowCut, 4000.0, 0.0, 0.7071, fs);
        auto highEnergy = [&] (const std::vector<float>& v)
        {
            double st[4][2] = {}, e = 0, total = 0;
            for (size_t i = 0; i < v.size(); ++i)
            {
                double u = v[i];
                for (auto& z : st)
                {
                    const double o = hpq.b0 * u + z[0];
                    z[0] = hpq.b1 * u - hpq.a1 * o + z[1];
                    z[1] = hpq.b2 * u - hpq.a2 * o;
                    u = o;
                }
                if (i > 9600) { e += u * u; total += (double) v[i] * v[i]; }
            }
            return 10.0 * std::log10 (e / total + 1e-300);
        };
        const double zipper = highEnergy (y[0]), floor = highEnergy (x[0]);
        check (zipper < -80.0, "MID and SIDE automated with a step every block: smooth, energy above 4 kHz " + fmt (zipper, 1)
                                   + " dB below the signal (measurement floor " + fmt (floor, 1) + " dB)");
    }
    {   // LINK (panel): turning MID moves SIDE by the same amount; host automation of MID leaves SIDE alone.
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("coherence-settings-test");
        UserSettings::setFileForTesting (dir.getChildFile ("COHERENCE.settings"));
        CoherenceProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        auto* ed = dynamic_cast<CoherenceEditor*> (e.get());
        auto& mid = *p.state().getParameter (id::mid);
        auto& side = *p.state().getParameter (id::side);
        auto val = [] (juce::RangedAudioParameter& q) { return q.convertFrom0to1 (q.getValue()); };
        p.state().getParameter (id::msLink)->setValueNotifyingHost (1.0f);
        mid.setValueNotifyingHost (mid.convertTo0to1 (-1.0f));
        side.setValueNotifyingHost (side.convertTo0to1 (2.0f));
        ed->findKnob (id::mid)->stepBy (5, false);        // +0.5 dB in Fine
        const float m1 = val (mid), s1 = val (side);
        ed->findKnob (id::side)->stepBy (-3, true);       // Shift: -0.03 dB
        const float m2 = val (mid), s2 = val (side);
        mid.setValueNotifyingHost (mid.convertTo0to1 (3.0f));   // host automation
        const float s3 = val (side);
        p.state().getParameter (id::msLink)->setValueNotifyingHost (0.0f);
        ed->findKnob (id::mid)->stepBy (1, false);
        const float s4 = val (side);
        const bool ok = std::abs (m1 + 0.5f) < 1e-4f && std::abs (s1 - 2.5f) < 1e-4f && std::abs (m2 + 0.53f) < 1e-4f && std::abs (s2 - 2.47f) < 1e-4f
                        && std::abs (s3 - s2) < 1e-6f && std::abs (s4 - s2) < 1e-6f;
        check (ok, "LINK on the panel keeps the MID/SIDE offset (MID " + fmt (m1, 2) + " / SIDE " + fmt (s1, 2) + ", then " + fmt (m2, 2) + " / " + fmt (s2, 2)
                       + "); host automation of MID and LINK off leave SIDE alone");
        e.reset();
        useDefaultTestSettings();
    }
}

//==============================================================================
// 48 kHz audit (v0.2.1 follow-up): every band type over the panel's full ranges at 48 kHz, against the analog
// prototype. Magnitude and phase are computed from the designs; the running filter equals its design (measured,
// "the running filter matches its design"), and centre gain, centre frequency and Q are measured from impulse
// responses through the engine.
void audit48()
{
    const double fs = 48000.0;
    line ("\n## 48 kHz audit: the EQ over the panel's full ranges\n");
    line ("Every band type on a dense grid of the panel's ranges at 48 kHz, against the analog prototype it targets.");
    line ("Magnitude error in dB (worst case per cell); phase error in degrees up to 10 kHz.\n");

    const auto freqs = logFreqs (20.0, 20000.0, 241);   // 1/24 octave
    struct Err { double lo = 0, mid = 0, top = 0, ph = 0; };   // 20 Hz-10 kHz, 10-16 kHz, 16-20 kHz, phase to 10 kHz
    auto errorsOf = [&] (const BandSettings& b)
    {
        const auto d = engineDesign (b.shape, b.frequency, b.gainDb, b.q, fs);
        Err e;
        for (double f : freqs)
        {
            const auto a = panelAnalog (b, f);
            if (std::abs (a) < 0.063 && (b.shape == Shape::lowCut || b.shape == Shape::highCut)) continue;   // more than 24 dB down in a cut (as in the main suite)
            const double w = 2.0 * kPi * f / fs;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
            const auto hd = (d.b0 + d.b1 * z1 + d.b2 * z2) / (1.0 + d.a1 * z1 + d.a2 * z2);
            const double m = std::abs (20.0 * std::log10 (std::abs (hd) / std::abs (a)));
            if (f <= 10000.0) { e.lo = std::max (e.lo, m); e.ph = std::max (e.ph, std::abs (wrapDeg (hd / a))); }
            else if (f <= 16000.0) e.mid = std::max (e.mid, m);
            else e.top = std::max (e.top, m);
        }
        return e;
    };

    const std::vector<double> qsBell { 0.3, 0.5, 0.7, 1.0, 1.4, 2.0, 3.0, 5.0, 7.0, 10.0 };
    const std::vector<double> qsShelf { 0.35, 0.5, 0.707, 1.0, 1.4, 2.0 };
    const std::vector<double> gainsAll { -15, -12, -9, -6, -3, -2, -1, -0.5, 0.5, 1, 2, 3, 6, 9, 12, 15 };
    struct Region { double lo, hi; const char* name; };
    const Region regions[] = { { 20, 250, "20-250 Hz" }, { 250, 1000, "250 Hz-1 kHz" }, { 1000, 4000, "1-4 kHz" }, { 4000, 8000, "4-8 kHz" },
                               { 8000, 12000, "8-12 kHz" }, { 12000, 16000, "12-16 kHz" }, { 16000, 20001, "16-20 kHz" } };

    auto shapeTable = [&] (Shape shape, const char* title, double fLo, double fHi, const std::vector<double>& qs, bool hasGain)
    {
        line (juce::String ("### ") + title + "\n");
        line ("| Band frequency | Gains up to 3 dB: error 20 Hz-10 kHz / 10-16 kHz / 16-20 kHz | Gains up to 15 dB: same | Phase to 10 kHz (all gains) |");
        line ("|---|---|---|---|");
        int n = 0, n05 = 0, n10 = 0, n25 = 0;
        for (const auto& r : regions)
        {
            if (r.hi <= fLo || r.lo >= fHi) continue;
            Err small, all;
            for (double f0 : logFreqs (std::max (r.lo, fLo), std::min (r.hi, fHi), 12))
                for (double q : qs)
                    for (double g : hasGain ? gainsAll : std::vector<double> { 0.0 })
                    {
                        const auto e = errorsOf ({ shape, f0, g, q });
                        auto upd = [&] (Err& t) { t.lo = std::max (t.lo, e.lo); t.mid = std::max (t.mid, e.mid); t.top = std::max (t.top, e.top); t.ph = std::max (t.ph, e.ph); };
                        upd (all);
                        if (std::abs (g) <= 3.0) upd (small);
                        const double w = std::max ({ e.lo, e.mid, e.top });
                        ++n; n05 += w < 0.05; n10 += w < 0.1; n25 += w < 0.25;
                    }
            auto cell = [&] (const Err& e) { return fmt (e.lo, 3) + " / " + fmt (e.mid, 3) + " / " + fmt (e.top, 2) + " dB"; };
            line (juce::String ("| ") + r.name + " | " + (hasGain ? cell (small) : juce::String ("-")) + " | " + cell (all) + " | " + fmt (all.ph, 2) + " deg |");
        }
        line ("\nOf " + juce::String (n) + " settings: " + fmt (100.0 * n05 / n, 1) + "% within 0.05 dB of analog everywhere from 20 Hz to 20 kHz, "
              + fmt (100.0 * n10 / n, 1) + "% within 0.1 dB, " + fmt (100.0 * n25 / n, 1) + "% within 0.25 dB.\n");
    };
    // Where the errors live: worst magnitude error from 20 Hz to 16 kHz, for bands set at or below 12 kHz, by Q and gain.
    auto byQ = [&] (Shape shape, const char* title, double fLo, const std::vector<double>& qs)
    {
        line (juce::String ("**") + title + ": by Q and gain** (band set " + fmt (fLo, 0) + " Hz to 12 kHz; worst error from 20 Hz to 16 kHz)\n");
        line ("| Q | Gains up to 1 dB | up to 3 dB | up to 6 dB | up to 15 dB |");
        line ("|---|---|---|---|---|");
        for (double q : qs)
        {
            double e[4] = { 0, 0, 0, 0 };
            const double lim[4] = { 1.0, 3.0, 6.0, 15.0 };
            for (double f0 : logFreqs (fLo, 12000.0, 40))
                for (double g : gainsAll)
                {
                    const auto r = errorsOf ({ shape, f0, g, q });
                    const double w = std::max (r.lo, r.mid);
                    for (int k = 0; k < 4; ++k) if (std::abs (g) <= lim[k]) e[k] = std::max (e[k], w);
                }
            line ("| " + fmt (q, 2) + " | " + fmt (e[0], 3) + " dB | " + fmt (e[1], 3) + " dB | " + fmt (e[2], 3) + " dB | " + fmt (e[3], 3) + " dB |");
        }
        line ("");
    };
    shapeTable (Shape::bell, "Bells (all four bands)", 20.0, 20000.0, qsBell, true);
    byQ (Shape::bell, "Bells", 20.0, qsBell);
    shapeTable (Shape::lowShelf, "Low shelf (LF)", 20.0, 1000.0, qsShelf, true);
    shapeTable (Shape::highShelf, "High shelf (HF)", 1000.0, 20000.0, qsShelf, true);
    byQ (Shape::highShelf, "High shelves", 1000.0, qsShelf);
    shapeTable (Shape::lowCut, "Low cut (LF)", 20.0, 1000.0, std::vector<double> { 0.3, 0.5, 0.707, 1.0, 2.0, 4.0 }, false);
    shapeTable (Shape::highCut, "High cut (HF)", 1000.0, 20000.0, std::vector<double> { 0.3, 0.5, 0.707, 1.0, 2.0, 4.0 }, false);

    // Measured from impulse responses through the engine: gain at f0, extremum frequency and Q of bells at 48 kHz.
    line ("### Bells measured from the running filter at 48 kHz\n");
    line ("Gain at the set frequency, the frequency of the extremum, and the half-gain bandwidth against the classic Q definition.\n");
    line ("| Band frequency | Worst gain error at f0 | Worst extremum shift, Q 1-5 | Worst Q error, Q 1-5 | Worst Q error, Q 0.5 |");
    line ("|---|---|---|---|---|");
    double worstG = 0;
    for (auto [lo, hi] : { std::pair { 30.0, 1000.0 }, std::pair { 1000.0, 4000.0 }, std::pair { 4000.0, 10000.0 }, std::pair { 10000.0, 16000.0 }, std::pair { 16000.0, 19000.0 } })
    {
        double ef = 0, eg = 0, eq = 0, eqWide = 0;
        for (double f0 : { lo, std::sqrt (lo * hi), hi })
            for (double q : { 0.5, 1.0, 2.0, 5.0 })
                for (double g : { 12.0, 6.0, 3.0, -6.0 })
                {
                    const auto m = measureBand (Shape::bell, f0, g, q, fs);
                    double a = f0 / 1.5, b = std::min (f0 * 1.5, 0.49 * fs);
                    const double sgn = g > 0 ? 1.0 : -1.0;
                    for (int i = 0; i < 60; ++i)
                    {
                        const double c1 = std::exp (std::log (a) + 0.382 * (std::log (b) - std::log (a)));
                        const double c2 = std::exp (std::log (a) + 0.618 * (std::log (b) - std::log (a)));
                        if (sgn * m.db (c1) > sgn * m.db (c2)) b = c2; else a = c1;
                    }
                    const double fp = std::sqrt (a * b), gp = m.db (fp);
                    if (q >= 1.0) ef = std::max (ef, std::abs (fp / f0 - 1.0) * 100.0);
                    eg = std::max (eg, std::abs (m.db (f0) - g));
                    const double half = gp * 0.5;
                    const double fl = crossing (m, std::max (5.0, fp / 64.0), fp, half, g > 0);
                    const double top = std::min (fp * 64.0, 0.4999 * fs);
                    if (sgn * m.db (top) < sgn * half)
                    {
                        const double fh = crossing (m, fp, top, half, g < 0);
                        const double nOct = std::log2 (fh / fl);
                        const double qm = std::sqrt (std::pow (2.0, nOct)) / (std::pow (2.0, nOct) - 1.0);
                        const double e = std::abs (qm / bellDesignQ (q, g) - 1.0) * 100.0;
                        if (q >= 1.0) eq = std::max (eq, e); else eqWide = std::max (eqWide, e);
                    }
                }
        worstG = std::max (worstG, eg);
        line ("| " + fmt (lo, 0) + " - " + fmt (hi, 0) + " Hz | " + fmt (eg, 4) + " dB | " + fmt (ef, 2) + " % | " + fmt (eq, 2) + " % | " + fmt (eqWide, 2) + " % |");
    }
    line ("");
    check (worstG < 0.005, "48 kHz: the gain at the set frequency equals the set gain for every measured bell (worst " + fmt (worstG, 4) + " dB)");
}

void cpu()
{
    line ("\n## CPU\n");
    for (double fs : { 48000.0, 96000.0 })
    {
        CoherenceProcessor p;
        p.setPlayConfigDetails (2, 2, fs, 256);
        p.prepareToPlay (fs, 256);
        auto& s = p.state();
        auto set = [&] (const juce::String& pid, float v) { auto* q = s.getParameter (pid); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        set (id::dynOn, 1); set ("lf_gain", 2); set ("lmf_gain", -2); set ("hmf_gain", 3); set ("hf_gain", 1.5f);
        set (id::side, 2); set (id::scHpf, 100);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        auto x = noise (256, 0.2f);
        const int blocks = (int) (fs * 20 / 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, x[(size_t) c].data(), 256);
            if (b % 50 == 0) set ("hmf_freq", 1000.0f + (float) (b % 1000));   // keep the smoothing busy
            p.processBlock (buf, midi);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        line ("- " + fmt (fs / 1000.0, 0) + " kHz stereo, all sections active: " + fmt (100.0 * secs / 20.0, 2) + " % of one core ("
              + fmt (secs / (20.0 * fs) * 1e9, 1) + " ns per sample frame)");
        check (secs / 20.0 < 0.05, "real-time load under 5% of one core at " + fmt (fs / 1000.0, 0) + " kHz");
    }
}

//==============================================================================
void snapshot (const juce::File& out, float scale, bool busy)
{
    CoherenceProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    auto& s = p.state();
    auto set = [&] (const juce::String& pid, float v) { auto* q = s.getParameter (pid); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    if (busy)
    {
        set ("hf_gain", 1.5f); set ("hf_freq", 12000.0f);
        set ("hmf_gain", -2.25f); set ("hmf_freq", 3150.0f); set ("hmf_q", 1.8f);
        set ("lmf_gain", 1.0f); set ("lmf_freq", 240.0f); set ("lmf_q", 0.8f);
        set ("lf_type", 2.0f); set ("lf_freq", 28.0f);
        set (id::dynOn, 1.0f); set (id::threshold, -22.0f); set (id::ratio, 3.0f); set (id::attack, 12.0f); set (id::release, 180.0f);
        set (id::makeup, 2.0f); set (id::mix, 85.0f); set (id::scHpf, 90.0f);
        set (id::side, 1.5f); set (id::mid, -0.5f); set (id::input, 1.0f); set (id::output, -1.5f);
    }
    // COHERENCE_SNAPSHOT_VIEW=mid or side: the panel in M/S, showing that channel.
    if (const auto view = juce::SystemStats::getEnvironmentVariable ("COHERENCE_SNAPSHOT_VIEW", {}); view.isNotEmpty())
    {
        set (id::eqMs, 1.0f);
        set (id::eqChannel, view == "side" ? 1.0f : 0.0f);
        set ("side_hf_gain", 3.0f); set ("side_hf_freq", 8000.0f); set ("side_hmf_gain", -2.0f); set ("side_lf_type", 2.0f); set ("side_lf_freq", 120.0f);
        set (id::eqOutput, -1.5f);
    }
    // Feed some program-like material so the meters show something.
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (4);
    std::normal_distribution<float> d (0.0f, 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* ce = dynamic_cast<CoherenceEditor*> (editor.get());
    ce->setScale (1.0f);
    float lp = 0, lp2 = 0;
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float n = d (rng);
            lp += 0.08f * (n - lp);
            lp2 += 0.3f * (d (rng) - lp2);
            const float kick = (float) std::exp (-(double) ((b * 512 + i) % 24000) / 3000.0) * (float) std::sin ((b * 512 + i) * 0.008);
            buf.setSample (0, i, 0.5f * lp + 0.08f * lp2 + 0.45f * kick);
            buf.setSample (1, i, 0.5f * lp - 0.03f * lp2 + 0.45f * kick);
        }
        p.processBlock (buf, midi);
        if (b > 150) ce->tickMetersForPreview (1);
    }
    editor->setVisible (true);
    juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) editor->getWidth() * scale), juce::roundToInt ((float) editor->getHeight() * scale), true);
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        editor->paintEntireComponent (g, true);
    }
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
}
// Minimal 32-bit float WAV writer (stereo).
void writeWav (const juce::File& f, const std::vector<std::vector<float>>& x, int fs)
{
    f.deleteFile();
    juce::FileOutputStream o (f);
    const uint32_t frames = (uint32_t) x[0].size(), ch = 2, bytes = frames * ch * 4;
    auto u32 = [&] (uint32_t v) { o.writeInt ((int) v); };
    auto u16 = [&] (uint16_t v) { o.writeShort ((short) v); };
    o.write ("RIFF", 4); u32 (36 + bytes); o.write ("WAVE", 4);
    o.write ("fmt ", 4); u32 (16); u16 (3); u16 ((uint16_t) ch); u32 ((uint32_t) fs); u32 ((uint32_t) fs * ch * 4); u16 ((uint16_t) (ch * 4)); u16 (32);
    o.write ("data", 4); u32 (bytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < ch; ++c)
            o.writeFloat (x[c][i]);
}

// The settings from REFERENCES/Q renders/HOW TO RENDER.md, rendered through the engine exactly like the plug-in.
void renderQSet (const juce::File& dir, int fs = 48000)
{
    dir.createDirectory();
    struct S { double f, q, g; const char* name; };
    const S set[] = { { 1000, 1.0, 12, "COH-ENGINE 1k Q1 +12" }, { 1000, 1.0, 3, "COH-ENGINE 1k Q1 +3" }, { 1000, 1.0, -12, "COH-ENGINE 1k Q1 -12" },
                      { 1000, 4.0, 6, "COH-ENGINE 1k Q4 +6" }, { 10000, 1.0, 6, "COH-ENGINE 10k Q1 +6" } };
    for (const auto& c : set)
    {
        Engine e;
        e.prepare (fs, 512, 2);
        auto st = neutral();
        st.bands[2] = { Shape::bell, c.f, c.g, c.q };
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) fs * 3, 0.0f));
        x[0][(size_t) fs] = x[1][(size_t) fs] = 0.5f;
        writeWav (dir.getChildFile (juce::String (c.name) + ".wav"), run (e, st, x), fs);
    }
    std::printf ("wrote %s\n", dir.getFullPathName().toRawUTF8());
}
// The settings of REFERENCES/EQ renders v2/HOW TO RENDER.md, rendered through the engine (names match
// Tools/reference_compare.py). Shelves and cuts use COHERENCE's default shape (Q 0.707).
void renderReferenceSet (const juce::File& dir)
{
    dir.createDirectory();
    struct R { const char* name; std::vector<BandSettings> bands; bool at96; };
    const auto B = [] (Shape s, double f, double q, double g) { return BandSettings { s, f, g, q }; };
    const std::vector<R> set {
        { "BELL 100 Q1 +6", { B (Shape::bell, 100, 1, 6) }, false },
        { "BELL 1k Q0.5 +6", { B (Shape::bell, 1000, 0.5, 6) }, false },
        { "BELL 1k Q8 -6", { B (Shape::bell, 1000, 8, -6) }, false },
        { "BELL 3k Q2 +6", { B (Shape::bell, 3000, 2, 6) }, false },
        { "BELL 5k Q0.7 +6", { B (Shape::bell, 5000, 0.7, 6) }, false },
        { "BELL 15k Q2 -9", { B (Shape::bell, 15000, 2, -9) }, false },
        { "BELL 18k Q1 +6", { B (Shape::bell, 18000, 1, 6) }, false },
        { "LS 100 +6", { B (Shape::lowShelf, 100, 0.707, 6) }, false },
        { "LS 300 -6", { B (Shape::lowShelf, 300, 0.707, -6) }, false },
        { "HS 3k +6", { B (Shape::highShelf, 3000, 0.707, 6) }, false },
        { "HS 10k -6", { B (Shape::highShelf, 10000, 0.707, -6) }, true },
        { "HS 15k +9", { B (Shape::highShelf, 15000, 0.707, 9) }, false },
        { "HP 40", { B (Shape::lowCut, 40, 0.707, 0) }, false },
        { "HP 100", { B (Shape::lowCut, 100, 0.707, 0) }, false },
        { "LP 8k", { B (Shape::highCut, 8000, 0.707, 0) }, false },
        { "LP 15k", { B (Shape::highCut, 15000, 0.707, 0) }, false },
        { "MULTI A", { B (Shape::lowShelf, 120, 0.707, 3.5), B (Shape::bell, 300, 1.4, -4), B (Shape::bell, 450, 0.8, 2.5), B (Shape::highShelf, 8000, 0.707, 1.5) }, false },
        { "BELL 1k Q1 +12", { B (Shape::bell, 1000, 1, 12) }, true },
        { "BELL 10k Q1 +6", { B (Shape::bell, 10000, 1, 6) }, true },
    };
    for (const auto& r : set)
        for (int fs : { 48000, 96000 })
        {
            if (fs == 96000 && ! r.at96) continue;
            Engine e;
            e.prepare (fs, 512, 2);
            auto st = neutral();
            if (r.bands.size() == 4)
                for (size_t k = 0; k < 4; ++k) st.bands[k] = r.bands[k];
            else
                st.bands[(size_t) bandSlotFor (r.bands[0].shape)] = r.bands[0];
            std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) fs * 3, 0.0f));
            x[0][(size_t) fs] = x[1][(size_t) fs] = 0.5f;
            writeWav (dir.getChildFile (juce::String ("COH-ENGINE ") + r.name + (fs == 96000 ? " 96k" : "") + ".wav"), run (e, st, x), fs);
        }
    std::printf ("wrote %s\n", dir.getFullPathName().toRawUTF8());
}
// Renders single-band settings in either EQ mode, for Tools/weiss_compare.py: an impulse of 0.5 at 1.0 s, 3 s,
// 32-bit float. The output is advanced by the engine's latency, as a host's delay compensation does.
// spec: "KIND F Q G;KIND F Q G;..." with KIND = BELL, LS, HS, HP, LP. Files: "<prefix> KIND F Q G.wav".
void renderModeSet (const juce::File& dir, bool weissMode, const juce::String& spec, int fs)
{
    dir.createDirectory();
    for (const auto& item : juce::StringArray::fromTokens (spec, ";", ""))
    {
        const auto t = juce::StringArray::fromTokens (item.trim(), " ", "");
        if (t.size() < 4) continue;
        const auto kind = t[0];
        const Shape shape = kind == "LS" ? Shape::lowShelf : kind == "HS" ? Shape::highShelf : kind == "HP" ? Shape::lowCut
                          : kind == "LP" ? Shape::highCut : Shape::bell;
        Engine e;
        e.prepare (fs, 512, 2);
        auto st = neutral();
        st.weissEq = weissMode;
        st.bands[(size_t) bandSlotFor (shape)] = BandSettings { shape, t[1].getDoubleValue(), t[3].getDoubleValue(), t[2].getDoubleValue() };
        const int lat = weissMode ? weissLatencySamples : 0;
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) fs * 3 + (size_t) lat, 0.0f));
        x[0][(size_t) fs] = x[1][(size_t) fs] = 0.5f;
        auto y = run (e, st, x);
        for (auto& c : y) { c.erase (c.begin(), c.begin() + lat); c.resize ((size_t) fs * 3); }
        writeWav (dir.getChildFile ((weissMode ? "COH-WEISS " : "COH-ENGINE ") + item.trim() + ".wav"), y, fs);
    }
}

// Minimal reader for 32-bit float WAV files (what Tools/comp_signals.py writes). Returns false otherwise.
bool readFloatWav (const juce::File& f, std::vector<std::vector<float>>& x, int& fs)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb) || mb.getSize() < 12) return false;
    const auto* d = static_cast<const uint8_t*> (mb.getData());
    auto u32 = [&] (size_t o) { return (uint32_t) d[o] | ((uint32_t) d[o + 1] << 8) | ((uint32_t) d[o + 2] << 16) | ((uint32_t) d[o + 3] << 24); };
    auto u16 = [&] (size_t o) { return (uint16_t) (d[o] | (d[o + 1] << 8)); };
    if (std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0) return false;
    int ch = 0, bits = 0, fmt = 0;
    for (size_t o = 12; o + 8 <= mb.getSize();)
    {
        const uint32_t len = u32 (o + 4);
        if (std::memcmp (d + o, "fmt ", 4) == 0) { fmt = u16 (o + 8); ch = u16 (o + 10); fs = (int) u32 (o + 12); bits = u16 (o + 22); }
        else if (std::memcmp (d + o, "data", 4) == 0)
        {
            if (! (fmt == 3 || fmt == 0xFFFE) || bits != 32 || ch < 1) return false;
            const size_t frames = len / (size_t) (4 * ch);
            x.assign (2, std::vector<float> (frames));
            for (size_t i = 0; i < frames; ++i)
                for (int c = 0; c < 2; ++c)
                    std::memcpy (&x[(size_t) c][i], d + o + 8 + (i * (size_t) ch + (size_t) std::min (c, ch - 1)) * 4, 4);
            return true;
        }
        o += 8 + len + (len & 1);
    }
    return false;
}

// Process a WAV file through the engine with the compressor set from key=value arguments (EQ out). Used to
// validate Tools/comp_analyze.py on known behaviour, and to render COHERENCE next to Pro-C 3 renders (COMPRESSOR-TESTING.md).
int renderFile (int argc, char** argv)
{
    std::vector<std::vector<float>> x;
    int fs = 0;
    if (! readFloatWav (juce::File (juce::String (argv[2])), x, fs)) { std::printf ("cannot read %s (32-bit float WAV expected)\n", argv[2]); return 1; }
    auto s = neutral();
    s.eqOn = false;
    s.dynOn = true;
    s.comp = { -20.0, 4.0, 10.0, 150.0, false, 0.0, 0.0, false, 1.0, 0.0 };
    for (int i = 4; i < argc; ++i)
    {
        const juce::String kv (argv[i]);
        const auto k = kv.upToFirstOccurrenceOf ("=", false, false);
        const double v = kv.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        if (k == "thr") s.comp.thresholdDb = v;
        else if (k == "ratio") s.comp.ratio = v;
        else if (k == "att") s.comp.attackMs = v;
        else if (k == "rel") s.comp.releaseMs = v;
        else if (k == "knee") s.comp.knee = v;
        else if (k == "makeup") s.comp.makeupDb = v;
        else if (k == "autorel") s.comp.autoRelease = v > 0.5;
        else if (k == "automakeup") s.comp.autoMakeup = v > 0.5;
        else if (k == "eqoutput") s.eqOutputDb = v;
        else if (k == "mix") s.comp.mix = v;
        else if (k == "schpf") s.comp.sidechainHpfHz = v;
        else { std::printf ("unknown setting %s\n", argv[i]); return 1; }
    }
    Engine e;
    e.prepare (fs, 512, 2);
    writeWav (juce::File (juce::String (argv[3])), run (e, s, x), fs);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::ScopedNoDenormals noDenormals;
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    useDefaultTestSettings();
    {
        juce::SharedResourcePointer<UserSettings> s;
        s->setGainStepping (GainStepping::precision);
    }

    if (argc >= 3 && juce::String (argv[1]) == "--render-q")
    {
        renderQSet (juce::File (juce::String (argv[2])), argc >= 4 ? juce::String (argv[3]).getIntValue() : 48000);
        return 0;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--audit48")
    {
        line ("# COHERENCE 48 kHz audit (v" JucePlugin_VersionString ")");
        audit48();
        juce::File (juce::String (argv[2])).replaceWithText (report);
        return failures == 0 ? 0 : 1;
    }

    if (argc >= 5 && juce::String (argv[1]) == "--render-mode")   // --render-mode <dir> weiss|normal "<spec>" [fs]
    {
        renderModeSet (juce::File (juce::String (argv[2])), juce::String (argv[3]) == "weiss", juce::String (argv[4]),
                       argc >= 6 ? juce::String (argv[5]).getIntValue() : 48000);
        return 0;
    }

    if (argc >= 4 && juce::String (argv[1]) == "--render-file")
        return renderFile (argc, argv);

    if (argc >= 3 && juce::String (argv[1]) == "--render-ref")
    {
        renderReferenceSet (juce::File (juce::String (argv[2])));
        return 0;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--approved-snapshot")
    {
        approvedSnapshot (juce::File (juce::String (argv[2])), argc >= 4 ? juce::String (argv[3]) : juce::String ("classic"), argc >= 5 ? juce::String (argv[4]).getFloatValue() : 1.6f, argc >= 6 && juce::String (argv[5]) == "side");
        return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
    {
        snapshot (juce::File (juce::String (argv[2])), argc >= 4 ? juce::String (argv[3]).getFloatValue() : 2.0f, argc < 5);
        return 0;
    }

    line ("# COHERENCE measurement report");
    line ("Generated by Tests/HeadlessTest.cpp (v" JucePlugin_VersionString "). Every figure is measured from the running code.");
    eqAccuracy();
    eqImplementation();
    eqStructures();
    eqFullRange();
    eqMeasured();
    eqInteraction();
    eqZipper();
    eqStress();
    eqPhaseAndRates();
    eqSlopes();
    eqBandsAgainstAnalog();
    audit48();
    displayMatchesDsp();
    midSideResolution();
    transparency();
    architecture();
    compressor();
    compressorLink();
    compressorControls();
    eqOutputChecks();
    msEqChecks();
    msPanelChecks();
    eqFocusChecks();
    abSurfaceChecks();
    abFirstSwitchChecks();
    defaultEqModeChecks();
    nonFiniteChecks();
    undoHistoryChecks();
    savedStateValueChecks();
    themeChecks();
    panelReachChecks();
    distortion();
    latency();
    nullTests();
    hostEdgeCases();
    gainSteppingAndWindow();
    gainSteppingExternal();
    controlSurfaceMeter();
    weissModeChecks();
    midSideChecks();
    processorState();
    cpu();
    cpuBreakdown();
    line ("\n" + juce::String (failures == 0 ? "All checks passed." : juce::String (failures) + " check(s) FAILED.")
          + (limits > 0 ? " " + juce::String (limits) + " documented limitation(s) (LIMIT)." : juce::String()));

    if (argc >= 3 && juce::String (argv[1]) == "--report")
        juce::File (juce::String (argv[2])).replaceWithText (report);
    return failures == 0 ? 0 : 1;
}
