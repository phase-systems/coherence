#!/usr/bin/env python3
"""Phase of bell renders against the analog bell they target (classic / MDWEQ6 Q definition).

Usage: python3 phase_analyze.py <folder> <prefix> [<prefix> ...]
Files are named "<prefix> <f>k Q<q> <+/-gain>.wav" (e.g. "MDW 1k Q1 +12.wav"): a 0.5 impulse at 1.0 s rendered
through the plug-in at 48 kHz.

For each render the script reports:
  * the pure delay (samples) that best explains the phase below 2 kHz: plug-in latency that the host did not
    compensate, or the delay of linear-phase resampling filters;
  * the phase difference from the analog bell after removing that delay: worst case 20 Hz-10 kHz and 10-20 kHz.
"""
import re
import sys
from pathlib import Path

import numpy as np

from q_analyze import load_ir


def analog_bell(freqs, f0, gain_db, q):
    """Classic bell: boost (s^2 + s G w0/Q + w0^2) / (s^2 + s w0/Q + w0^2); a cut is the reciprocal of the boost."""
    G = 10 ** (abs(gain_db) / 20.0)
    s = 2j * np.pi * freqs
    w0 = 2 * np.pi * f0
    h = (s * s + s * G * w0 / q + w0 * w0) / (s * s + s * w0 / q + w0 * w0)
    return h if gain_db >= 0 else 1.0 / h


def spectrum(ir, fs, freqs, offset):
    """Complex response at freqs, with time measured from `offset` samples (where the input impulse was)."""
    n = np.arange(len(ir)) - offset
    return np.array([np.sum(ir * np.exp(-2j * np.pi * f * n / fs)) for f in freqs])


def parse(stem, prefix):
    m = re.match(re.escape(prefix) + r" ([\d.]+)k Q([\d.]+) ([+-][\d.]+)$", stem)
    if not m:
        return None
    return float(m.group(1)) * 1000.0, float(m.group(2)), float(m.group(3))


def main(folder, prefixes):
    freqs = np.geomspace(20.0, 20000.0, 400)
    print("| Render | Delay that best explains the phase | Phase vs analog, 20 Hz-10 kHz | 10-20 kHz | At 20 kHz |")
    print("|---|---|---|---|---|")
    for prefix in prefixes:
        for p in sorted(Path(folder).glob(prefix + " *.wav")):
            spec = parse(p.stem, prefix)
            if spec is None:
                continue
            f0, q, g = spec
            fs, ir = load_ir(p)
            offset = int(0.1 * fs)   # load_ir starts the window 0.1 s before the input impulse
            # trim the window to the significant part for speed
            ir = ir[: int(0.6 * fs)]
            h = spectrum(ir, fs, freqs, offset)
            ref = analog_bell(freqs, f0, g, q)
            dphi = np.unwrap(np.angle(h / ref))
            # Pure delay from the low-frequency phase slope (least squares through the origin, f < 2 kHz).
            lo = freqs < 2000.0
            w = 2 * np.pi * freqs[lo] / fs
            delay = -np.sum(w * dphi[lo]) / np.sum(w * w)
            delay_r = round(delay)
            if abs(delay - delay_r) < 0.5:
                # Integer latency (0 for zero-latency plug-ins): remove exactly that, so the residual phase of a
                # minimum-phase filter is not absorbed into a fractional "delay".
                delay = float(delay_r)
            resid = np.degrees(dphi + 2 * np.pi * freqs * delay / fs)
            resid -= 360.0 * np.round(resid[0] / 360.0)
            a = np.max(np.abs(resid[freqs <= 10000.0]))
            b = np.max(np.abs(resid[freqs > 10000.0]))
            print("| %s | %.2f samples | %.2f deg | %.1f deg | %+.1f deg |" % (p.stem, delay, a, b, resid[-1]))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
