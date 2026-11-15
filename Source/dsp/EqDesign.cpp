#include "EqDesign.h"
#include "EqFitTable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace coherence::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;

struct Analog
{
    double n2, n1, n0, d2, d1, d0;   // (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0)
};

double clampF0 (double f0, double fs) { return std::clamp (f0, 1.0, maxFrequencyRatio * fs); }

Analog prototype (Shape shape, double f0, double gainDb, double q)
{
    const double w = 2.0 * pi * f0;
    const double A = std::pow (10.0, gainDb / 40.0);
    const double sA = std::sqrt (A);
    switch (shape)
    {
        case Shape::bell:      return { 1.0, A * w / q, w * w, 1.0, w / (A * q), w * w };
        case Shape::lowShelf:  return { A, A * sA * w / q, A * A * w * w, A, sA * w / q, w * w };
        case Shape::highShelf: return { A * A, A * sA * w / q, A * w * w, 1.0, sA * w / q, A * w * w };
        case Shape::lowCut:    return { 1.0, 0.0, 0.0, 1.0, w / q, w * w };
        case Shape::highCut:   return { 0.0, 0.0, w * w, 1.0, w / q, w * w };
    }
    return { 1, 0, 0, 1, 0, 0 };
}

double mag2 (const Analog& h, double omega)   // omega in rad/s
{
    const double o2 = omega * omega;
    const double nr = h.n0 - h.n2 * o2, ni = h.n1 * omega;
    const double dr = h.d0 - h.d2 * o2, di = h.d1 * omega;
    return (nr * nr + ni * ni) / (dr * dr + di * di);
}

Biquad invert (const Biquad& f)
{
    // 1/H: numerator and denominator swap; normalise by the new a0 (= old b0).
    const double s = 1.0 / f.b0;
    return { s, f.a1 * s, f.a2 * s, f.b1 * s, f.b2 * s };
}

// Impulse-invariant pole pair for the analog denominator.
void matchedPoles (const Analog& h, double fs, double& a1, double& a2)
{
    const double wp = std::sqrt (h.d0 / h.d2) / fs;              // rad/sample
    const double zeta = h.d1 / (2.0 * std::sqrt (h.d0 * h.d2));
    const double e = std::exp (-zeta * wp);
    if (zeta <= 1.0)
        a1 = -2.0 * e * std::cos (std::sqrt (1.0 - zeta * zeta) * wp);
    else
        a1 = -2.0 * e * std::cosh (std::sqrt (zeta * zeta - 1.0) * wp);
    a2 = e * e;
}

Biquad matchedCore (Shape shape, double f0, double gainDb, double q, double fs)
{
    const auto h = prototype (shape, f0, gainDb, q);
    double a1, a2;
    matchedPoles (h, fs, a1, a2);

    const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
    const double A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
    const double A2 = -4.0 * a2;
    const double wm = 2.0 * pi * f0 / fs;
    const double s = std::sin (0.5 * wm);
    const double p1 = s * s, p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
    const double den = A0 * p0 + A1 * p1 + A2 * p2;

    Biquad r;
    r.a1 = a1;
    r.a2 = a2;

    if (shape == Shape::lowCut)
    {
        const double b0 = std::sqrt (std::max (0.0, den)) * q / (4.0 * p1);
        r.b0 = b0; r.b1 = -2.0 * b0; r.b2 = b0;
        return r;
    }
    if (shape == Shape::highCut)
    {
        const double R1 = den * q * q;
        const double B0 = A0, B1 = std::max (0.0, (R1 - B0 * p0) / p1);
        const double b0 = 0.5 * (std::sqrt (B0) + std::sqrt (B1));
        r.b0 = b0; r.b1 = std::sqrt (B0) - b0; r.b2 = 0.0;
        return r;
    }

    // Bells and shelves: |N|^2 matched at DC, Nyquist and f0.
    const double B0 = mag2 (h, 0.0) * A0;
    const double B1 = mag2 (h, pi * fs) * A1;
    const double B2 = (mag2 (h, wm * fs) * den - B0 * p0 - B1 * p1) / p2;
    const double W = 0.5 * (std::sqrt (B0) + std::sqrt (B1));
    const double disc = std::sqrt (std::max (0.0, W * W + B2));
    r.b1 = 0.5 * (std::sqrt (B0) - std::sqrt (B1));
    r.b0 = 0.5 * (W + disc);
    r.b2 = 0.5 * (W - disc);
    return r;
}
} // namespace

double analogMagnitudeDb (Shape shape, double f0, double gainDb, double q, double f)
{
    return 10.0 * std::log10 (mag2 (prototype (shape, f0, gainDb, q), 2.0 * pi * f) + 1e-300);
}

std::complex<double> analogResponse (Shape shape, double f0, double gainDb, double q, double f)
{
    const auto h = prototype (shape, f0, gainDb, q);
    const std::complex<double> s (0.0, 2.0 * pi * f);
    return (h.n2 * s * s + h.n1 * s + h.n0) / (h.d2 * s * s + h.d1 * s + h.d0);
}

Biquad designMatched (Shape shape, double f0, double gainDb, double q, double fs)
{
    f0 = clampF0 (f0, fs);
    q = std::clamp (q, 0.05, 50.0);
    // Design the variant whose poles sit at or below f0 (bells and low shelves: the boost; high shelves: the
    // cut). The other sign is its exact inverse, so boost and cut are perfect reciprocals.
    if ((shape == Shape::bell || shape == Shape::lowShelf) && gainDb < 0.0)
        return invert (matchedCore (shape, f0, -gainDb, q, fs));
    if (shape == Shape::highShelf && gainDb > 0.0)
        return invert (matchedCore (shape, f0, -gainDb, q, fs));
    return matchedCore (shape, f0, gainDb, q, fs);
}

Biquad designBilinear (Shape shape, double f0, double gainDb, double q, double fs)
{
    f0 = clampF0 (f0, fs);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * f0 / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (shape)
    {
        case Shape::bell:
            b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A;
            break;
        case Shape::lowShelf:
        {
            const double t = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) - (A - 1) * c + t); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - t);
            a0 = (A + 1) + (A - 1) * c + t; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - t;
            break;
        }
        case Shape::highShelf:
        {
            const double t = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) + (A - 1) * c + t); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - t);
            a0 = (A + 1) - (A - 1) * c + t; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - t;
            break;
        }
        case Shape::lowCut:
            b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al;
            break;
        case Shape::highCut:
            b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al;
            break;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

SvfParams svfFromBiquad (const Biquad& f)
{
    // Poles: D(z) = (1-z^-1)^2 + gk(1-z^-2) + g^2(1+z^-1)^2, normalised.
    const double sumP = std::max (1e-300, 1.0 + f.a1 + f.a2);
    const double difP = std::max (1e-300, 1.0 - f.a1 + f.a2);
    SvfParams p;
    p.g = std::sqrt (sumP / difP);
    p.k = 2.0 * (1.0 - f.a2) / (p.g * difP);
    // Numerator in the HP/BP/LP basis, then expressed as the SVF output mix.
    const double c = 4.0 / difP;
    const double hp = c * (f.b0 - f.b1 + f.b2) * 0.25;
    const double bp = c * (f.b0 - f.b2) * 0.5;
    const double lp = c * (f.b0 + f.b1 + f.b2) * 0.25;
    p.m0 = hp;
    p.m1 = bp / p.g - p.k * hp;
    p.m2 = lp / (p.g * p.g) - hp;
    return p;
}

Biquad biquadFromSvf (const SvfParams& p)
{
    const double g = p.g, k = p.k;
    const double d0 = 1.0 + g * k + g * g, d1 = 2.0 * g * g - 2.0, d2 = 1.0 - g * k + g * g;
    // y = m0*x + m1*BP + m2*LP, BP numerator g(1 - z^-2), LP numerator g^2(1 + z^-1)^2
    const double n0 = p.m0 * d0 + p.m1 * g + p.m2 * g * g;
    const double n1 = p.m0 * d1 + p.m2 * 2.0 * g * g;
    const double n2 = p.m0 * d2 - p.m1 * g + p.m2 * g * g;
    return { n0 / d0, n1 / d0, n2 / d0, d1 / d0, d2 / d0 };
}

SvfCoeffs coeffsFromSvf (const SvfParams& p)
{
    SvfCoeffs c;
    c.a1 = 1.0 / (1.0 + p.g * (p.g + p.k));
    c.a2 = p.g * c.a1;
    c.a3 = p.g * c.a2;
    c.m0 = p.m0;
    c.m1 = p.m1;
    c.m2 = p.m2;
    return c;
}

double biquadMagnitudeDb (const Biquad& f, double freq, double fs)
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * pi * freq / fs);
    const auto z2 = z1 * z1;
    const auto h = (f.b0 + f.b1 * z1 + f.b2 * z2) / (1.0 + f.a1 * z1 + f.a2 * z2);
    return 20.0 * std::log10 (std::abs (h) + 1e-300);
}

//==============================================================================
// Fitted design: trilinear interpolation of (log g, log k, m0, m1, m2) over (log f0/fs, log Q, gain).
namespace
{
const fit::Table* tableFor (Shape shape, double fs)
{
    if (! fit::available)
        return nullptr;
    const bool high = fs >= fit::familySplitRate;
    if (shape == Shape::bell)
        return high ? fit::bellBPtr : fit::bellAPtr;
    if (shape == Shape::highShelf)
        return high ? fit::highShelfBPtr : fit::highShelfAPtr;
    return nullptr;
}

// Interpolated correction (d log g, d log k, d m0, d m1, d m2) to the matched design.
std::array<double, 5> lookupCorrection (const fit::Table& t, double x, double q, double gainDb)
{
    const double ux = std::log (x / t.xMin) / std::log (t.xMax / t.xMin) * (t.numX - 1);
    const double uq = std::log (q / t.qMin) / std::log (t.qMax / t.qMin) * (t.numQ - 1);
    const double fx = std::clamp (ux, 0.0, (double) t.numX - 1.000001);
    const double fq = std::clamp (uq, 0.0, (double) t.numQ - 1.000001);
    const int ix = (int) fx, iq = (int) fq;
    const double tx = fx - ix, tq = fq - iq;

    const double g = std::clamp (gainDb, fit::gains[0], fit::gains[fit::numGains - 1]);
    int ig = 0;
    while (ig < fit::numGains - 2 && g > fit::gains[ig + 1])
        ++ig;
    const double tg = (g - fit::gains[ig]) / (fit::gains[ig + 1] - fit::gains[ig]);

    std::array<double, 5> v {};
    for (int c = 0; c < 8; ++c)
    {
        const int dq = c & 1, dg = (c >> 1) & 1, dx = (c >> 2) & 1;
        const double w = (dq ? tq : 1.0 - tq) * (dg ? tg : 1.0 - tg) * (dx ? tx : 1.0 - tx);
        if (w == 0.0)
            continue;
        const float* e = t.data + ((((size_t) (iq + dq) * fit::numGains + (size_t) (ig + dg)) * (size_t) t.numX + (size_t) (ix + dx)) * 5);
        for (size_t k = 0; k < 5; ++k)
            v[k] += w * (double) e[k];
    }
    return v;
}
} // namespace

// Trim the band-pass mix (m1) so the gain at f0 equals the analog target exactly. BP is zero at DC and
// Nyquist, so this leaves both ends untouched and only removes the interpolation residual at the centre.
static void trimGainAtF0 (SvfParams& p, double w0, double targetLinear)
{
    const std::complex<double> z1 = std::polar (1.0, -w0);
    const auto z2 = z1 * z1;
    const double g = p.g, k = p.k;
    const auto D = (1.0 + g * k + g * g) + (2.0 * g * g - 2.0) * z1 + (1.0 - g * k + g * g) * z2;
    const auto BP = g * (1.0 - z2) / D;
    const auto LP = g * g * (1.0 + z1) * (1.0 + z1) / D;
    const auto A = p.m0 + p.m2 * LP;
    const double a = std::norm (BP);
    const double b = 2.0 * (A * std::conj (BP)).real();
    const double c = std::norm (A) - targetLinear * targetLinear;
    const double disc = b * b - 4.0 * a * c;
    if (a <= 0.0 || disc < 0.0)
        return;
    const double r1 = (-b + std::sqrt (disc)) / (2.0 * a), r2 = (-b - std::sqrt (disc)) / (2.0 * a);
    p.m1 = std::abs (r1 - p.m1) < std::abs (r2 - p.m1) ? r1 : r2;
}

bool fittedDesignCovers (Shape shape, double f0, double q, double fs)
{
    const auto* t = tableFor (shape, fs);
    if (t == nullptr)
        return false;
    const double x = clampF0 (f0, fs) / fs;
    return x <= t->xMax && q >= t->qMin && q <= t->qMax;
}

SvfParams designBand (Shape shape, double f0, double gainDb, double q, double fs)
{
    f0 = clampF0 (f0, fs);

    // Fitted bells and high shelves: the cut is the exact inverse of the fitted boost, so boost and cut stay
    // perfect reciprocals (the inverse is used only when it is stable, which the fitted boosts are by design).
    if (gainDb < 0.0 && tableFor (shape, fs) != nullptr)
    {
        const auto boost = biquadFromSvf (designBand (shape, f0, -gainDb, q, fs));
        const auto inv = invert (boost);
        if (std::abs (inv.a2) < 0.999999 && std::abs (inv.a1) < 1.0 + inv.a2 - 1e-9)
            return svfFromBiquad (inv);
    }

    const auto m = svfFromBiquad (designMatched (shape, f0, gainDb, q, fs));
    const auto* t = tableFor (shape, fs);
    if (t == nullptr)
        return m;

    // Fitted = matched + interpolated correction. Below the grid the matched design is already accurate to a
    // few thousandths of a dB, so the correction fades out towards DC (continuous across the grid edge).
    const double x = f0 / fs;
    const double w = x >= t->xMin ? 1.0 : x / t->xMin;
    const auto d = lookupCorrection (*t, std::max (x, t->xMin), std::clamp (q, t->qMin, t->qMax), gainDb);
    SvfParams r;
    r.g = m.g * std::exp (w * d[0]);
    r.k = m.k * std::exp (w * d[1]);
    r.m0 = m.m0 + w * d[2];
    r.m1 = m.m1 + w * d[3];
    r.m2 = m.m2 + w * d[4];
    // Bells: make the centre gain exact (the band-pass term dominates at f0, so the trim is tiny and local).
    // Shelves are fitted with the centre point weighted already; a trim through the band-pass term would be
    // too blunt for them near Nyquist.
    if (shape == Shape::bell)
        trimGainAtF0 (r, 2.0 * pi * f0 / fs, std::pow (10.0, gainDb / 20.0));
    return r;
}
//==============================================================================
// Weiss mode (DESIGN.md section 15)

const double weissHalfband[weissHalfbandTaps] = {
    0.633741788757, -0.203543918903, 0.113241018142, -0.072153597784, 0.048163769177, -0.032450013796,
    0.021641067670, -0.014137293036, 0.008910323479, -0.005427090534, 0.003113252801, -0.001697363140,
    0.000850123081, -0.000381507385, 0.000162370548, -0.000032929076
};

double weissShelfQ (double panelQ, double gainDb)
{
    // Fitted to 16 Weiss low-shelf renders (gains -6, +-3, +6, +12 dB; Q 0.5 - 4): within 0.0016 dB.
    // Confirmed out of sample on the high shelves, including +9 dB. Beyond 12 dB it is an extrapolation.
    constexpr double k = 0.9577883, c = 0.34193424, e = -0.83515159, u = 2.07234491;
    const double A = std::pow (10.0, std::abs (gainDb) / 40.0);
    const double a = k * (std::pow (A, e) + u) / (A + u);
    return 1.0 / (a * (1.0 / std::max (0.05, panelQ) + c));
}

SvfParams designWeiss (Shape shape, double f0, double gainDb, double q, double rate)
{
    f0 = std::clamp (f0, 1.0, maxFrequencyRatio * rate);
    q = std::max (0.05, q);
    const double A = std::pow (10.0, gainDb / 40.0);   // signed gain: A < 1 for cuts
    double fc = f0;                                     // natural frequency of the poles
    double k = 1.0 / q, n2 = 1.0, n1 = 0.0, n0 = 1.0;   // H(s) = (n2 s^2 + n1 wc s + n0 wc^2) / (s^2 + k wc s + wc^2)
    switch (shape)
    {
        case Shape::bell:
        {
            const double G = A * A;
            if (gainDb >= 0.0) { k = 1.0 / q; n1 = G / q; }
            else               { k = 1.0 / (G * q); n1 = 1.0 / q; }   // G < 1: the cut is the inverse of the boost
            break;
        }
        case Shape::lowShelf:
        case Shape::highShelf:
        {
            const double qs = weissShelfQ (q, gainDb);
            const double r = std::pow (10.0, std::abs (gainDb) / 80.0);
            const double mid = shape == Shape::lowShelf ? f0 * r : f0 / r;   // analog midpoint
            k = 1.0 / qs;
            n1 = A / qs;
            if (shape == Shape::lowShelf) { fc = mid / std::sqrt (A); n2 = 1.0; n0 = A * A; }
            else                          { fc = mid * std::sqrt (A); n2 = A * A; n0 = 1.0; }
            break;
        }
        case Shape::lowCut:  n2 = 1.0; n1 = 0.0; n0 = 0.0; break;
        case Shape::highCut: n2 = 0.0; n1 = 0.0; n0 = 1.0; break;
    }
    // Bilinear transform prewarped at the set frequency f0: g = (wc / K) with K = w0 / tan (w0 / (2 rate)).
    SvfParams p;
    p.g = std::tan (pi * f0 / rate) * (fc / f0);
    p.k = k;
    p.m0 = n2;
    p.m1 = n1 - n2 * k;
    p.m2 = n0 - n2;
    return p;
}
} // namespace coherence::dsp
