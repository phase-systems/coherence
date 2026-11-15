#!/usr/bin/env python3
"""Offline experiment (no effect on the build): magnitude-only fit vs complex (magnitude + phase) fit.

The v0.2 table fits |H| to the analog prototype over 10 Hz - 20 kHz (at 44.1 kHz) and leaves the response
above 20 kHz free. A minimum-phase filter's phase is set by its magnitude at *all* frequencies up to Nyquist, so
a free top band can cost phase accuracy below it. This script fits the same second-order section three ways and
reports magnitude and phase error against analog, at the sample rate given:

  matched   Vicanek matched design (no optimisation)
  magnitude current method (eq_optimize3: minimax dB error, exact gain at f0)
  complex   minimax of the complex error |H/Ha - 1| expressed in dB (20 log10(1+e)), exact gain at f0;
            1 degree of phase error then counts like 0.15 dB of magnitude error

Usage: python3 phase_fit_experiment.py [fs]
"""
import sys

import numpy as np
from scipy.optimize import minimize

from eq_design import H_analog, H_digital, matched, proto
from eq_optimize2 import biquad_from_svf, svf_vec


def fit(kind, x, g, Q, band, mode):
    lo, hi = band
    W = np.geomspace(2 * np.pi * lo, 2 * np.pi * hi, 280)
    num, den = proto(kind, x, g, Q)
    Ha = H_analog(num, den, W)
    w0 = np.array([2 * np.pi * x])
    ref0 = 20 * np.log10(np.abs(H_analog(num, den, w0)))
    bm, am = matched(kind, x, g, Q, 1.0)
    vm = svf_vec(bm, am)
    if mode == "matched":
        return vm

    def errs(v, w=W, ha=Ha):
        b, a = biquad_from_svf(v)
        hd = H_digital(b, a, w)
        if mode == "magnitude":
            return 20 * np.log10(np.abs(hd) + 1e-30) - 20 * np.log10(np.abs(ha))
        return 20 * np.log10(1.0 + np.abs(hd / ha - 1.0))

    def f0err(v):
        b, a = biquad_from_svf(v)
        return 20 * np.log10(np.abs(H_digital(b, a, w0))[0]) - ref0[0]

    def cost(v, p):
        b, a = biquad_from_svf(v)
        r = np.max(np.abs(np.roots(b)))
        pen = 1e3 * max(0.0, r - 0.9995)
        return np.mean(np.abs(errs(v)) ** p) ** (1 / p) + pen + 50.0 * abs(f0err(v))

    best = vm
    for p in (8, 30):
        best = minimize(lambda v: cost(v, p), best, method="Nelder-Mead",
                        options=dict(maxiter=4000, xatol=1e-11, fatol=1e-12, adaptive=True)).x
    return best


def report(kind, f0, g, Qd, fs, name):
    x = f0 / fs
    band = (10.0 / 44100.0, 20000.0 / 44100.0) if fs < 80000 else (10.0 / 88200.0, 24000.0 / 88200.0)
    f = np.geomspace(20.0, 20000.0, 500)
    W = 2 * np.pi * f / fs
    num, den = proto(kind, x, g, Qd)
    Ha = H_analog(num, den, W)
    out = []
    for mode in ("matched", "magnitude", "complex"):
        v = fit(kind, x, g, Qd, band, mode)
        b, a = biquad_from_svf(v)
        hd = H_digital(b, a, W)
        m = np.abs(20 * np.log10(np.abs(hd / Ha)))
        ph = np.abs(np.degrees(np.angle(hd / Ha)))
        lo = f <= 10000
        out.append("%.3f dB / %.2f deg / %.1f deg" % (np.max(m), np.max(ph[lo]), np.max(ph[~lo])))
    print("| %s | %s |" % (name, " | ".join(out)))


if __name__ == "__main__":
    fs = float(sys.argv[1]) if len(sys.argv) > 1 else 48000.0
    print("At %.1f kHz. Each cell: max magnitude error 20 Hz-20 kHz / max phase error to 10 kHz / 10-20 kHz.\n" % (fs / 1000))
    print("| Setting (panel) | matched | magnitude fit (v0.2) | complex fit |")
    print("|---|---|---|---|")
    bq = lambda q, g: q / 10 ** (abs(g) / 40.0)
    for f0, q, g in ((1000, 1.0, 12), (1000, 1.0, 3), (1000, 4.0, 6), (5000, 0.7, 6), (10000, 1.0, 6), (15000, 2.0, 9), (18000, 1.0, 6)):
        report("bell", f0, g, bq(q, g), fs, "Bell %g Hz Q%g +%g" % (f0, q, g))
    for f0, q, g in ((3000, 0.707, 6), (10000, 0.707, 6), (15000, 1.0, 9)):
        report("highshelf", f0, g, q, fs, "High shelf %g Hz Q%g %g dB" % (f0, q, g))
