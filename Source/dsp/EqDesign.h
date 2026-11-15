// COHERENCE - EQ filter design.
//
// Every band is a second-order section realised as a trapezoidal state-variable filter (Simper) running in
// double precision. The SVF is used only as the *structure*: its coefficients come from a design that targets
// the analog prototype's magnitude response directly, so the response does not cramp towards Nyquist the way
// a bilinear-transform (RBJ cookbook) filter does.
//
// Two designs are used:
//  - Fitted (bells, high shelves): coefficients optimised offline against the analog prototype over
//    10 Hz .. 0.907 x Nyquist (minimax-like, see Tools/eq_optimize2.py), stored on a grid of
//    (f0/fs, Q, gain) and interpolated in SVF parameter space, which keeps every interpolated filter stable.
//  - Matched (low shelves, cut filters, anything outside the fitted grid): Vicanek's matched second-order
//    design (poles by impulse invariance, numerator fitted to the analog magnitude at DC, f0 and Nyquist).
//
// Boost and cut are exact reciprocals: the cut is the inverse of the boost with the same frequency and Q.

#pragma once

#include <cmath>
#include <complex>

namespace coherence::dsp
{
enum class Shape
{
    bell,
    lowShelf,
    highShelf,
    lowCut,    // 12 dB/oct high-pass with resonance Q
    highCut    // 12 dB/oct low-pass with resonance Q
};

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

// Trapezoidal SVF: per sample
//   v3 = x - ic2;  v1 = a1*ic1 + a2*v3;  v2 = ic2 + a2*ic1 + a3*v3;  ic1 = 2v1 - ic1;  ic2 = 2v2 - ic2
//   y  = m0*x + m1*v1 + m2*v2
struct SvfCoeffs
{
    double a1 = 0.5, a2 = 0.25, a3 = 0.125, m0 = 1.0, m1 = 0.0, m2 = 0.0;
};

// The SVF in its natural parameters (g = tan-like frequency warp, k = damping) plus output mix.
struct SvfParams
{
    double g = 1.0, k = 2.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
};

// The analog prototype (what the design is measured against). f in Hz.
double analogMagnitudeDb (Shape, double f0, double gainDb, double q, double f);
// Its complex response (magnitude and phase), for phase measurements. Every prototype is minimum phase.
std::complex<double> analogResponse (Shape, double f0, double gainDb, double q, double f);

Biquad designMatched (Shape, double f0, double gainDb, double q, double sampleRate);
Biquad designBilinear (Shape, double f0, double gainDb, double q, double sampleRate);   // RBJ, for comparison only

SvfParams svfFromBiquad (const Biquad&);
Biquad biquadFromSvf (const SvfParams&);
SvfCoeffs coeffsFromSvf (const SvfParams&);

// The design used by the plug-in: fitted where available, matched otherwise.
SvfParams designBand (Shape, double f0, double gainDb, double q, double sampleRate);
bool fittedDesignCovers (Shape, double f0, double q, double sampleRate);

// Magnitude response of a biquad at frequency f (Hz).
double biquadMagnitudeDb (const Biquad&, double f, double sampleRate);

// The Q a band actually runs at. Bells use the set value; on shelves Q shapes the knee (0.35..2.0) and on cuts
// it sets the resonance (0.3..4.0). The panel shows this value, so the readout always matches the DSP.
inline double effectiveQ (Shape shape, double q)
{
    if (shape == Shape::lowShelf || shape == Shape::highShelf)
        return q < 0.35 ? 0.35 : (q > 2.0 ? 2.0 : q);
    if (shape == Shape::lowCut || shape == Shape::highCut)
        return q < 0.3 ? 0.3 : (q > 4.0 ? 4.0 : q);
    return q;
}

// Q convention (v0.2). The panel's Q is the classic analog Q of the boost: the band-pass Q of
//   H(s) = (s^2 + s*G*w0/Q + w0^2) / (s^2 + s*w0/Q + w0^2),   G = 10^(gain/20),
// with a cut the exact reciprocal of the boost. This is how MDWEQ6 defines Q (measured from renders, see
// DESIGN.md). The design routines work with the RBJ "half-gain" Q, which for the same curve is Q / 10^(|gain|/40).
inline double bellDesignQ (double panelQ, double gainDb)
{
    const double a = std::pow (10.0, std::abs (gainDb) / 40.0);
    return panelQ / a;
}

// Parameter limits the design honours (the plug-in clamps to these).
constexpr double maxFrequencyRatio = 0.47;   // f0 <= 0.47 fs

//==============================================================================
// Weiss mode (DESIGN.md section 15): the Weiss EQ MP model measured from your renders. Every band is an analog
// prototype mapped with the bilinear transform at the processing rate (twice the session rate), prewarped at the
// set frequency, and realised as an SVF.
//   bell:   the classic analog bell (the same Q as MDWEQ6 and COHERENCE's normal mode);
//   shelf:  second-order shelf whose set frequency is its pole frequency (the midpoint is 10^(|gain|/80) away) and
//           whose Q maps to the shelf's own Q by 1 / Qs = a(|gain|) (1 / Q + c) (weissShelfQ);
//   cuts:   second-order Butterworth-style high-/low-pass with the panel Q (not measured against Weiss).
double weissShelfQ (double panelQ, double gainDb);
SvfParams designWeiss (Shape, double f0, double gainDb, double q, double processRate);

// The Weiss-mode resampler: a 63-tap linear-phase half-band interpolator (identified from your renders by least
// squares); decimation takes every second sample. With a flat EQ the chain is an exact delay of
// weissLatencySamples at the session rate.
constexpr int weissHalfbandTaps = 16;   // h(±1), h(±3), ... h(±31); h(0) = 1, the other even taps are 0
extern const double weissHalfband[weissHalfbandTaps];
constexpr int weissLatencySamples = 15;
} // namespace coherence::dsp
