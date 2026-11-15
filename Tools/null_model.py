#!/usr/bin/env python3
"""What a null test between two EQ renders leaves, and why.

Usage: python3 null_model.py <reference folder> <ref prefix> <test folder> <test prefix>

For each pair of impulse-response renders (same names after the prefix, 0.5 impulse at 1.0 s, same rate), this
predicts the residual of a sample-aligned null test (test minus reference, polarity inverted correctly, gains
matched) for three source spectra, and splits it exactly into its parts:

    |Ht - Hr|^2  =  (|Ht| - |Hr|)^2            magnitude part
                 +  2 |Ht| |Hr| (1 - cos dphi)   phase part

by frequency band. The prediction is checked against an actual time-domain null of 20 s of pink noise.
It also shows how misalignment, polarity and gain errors in the test itself would change the result.

Residual levels are RMS of the difference relative to RMS of the reference output, in dB.
"""
import sys
from pathlib import Path

import numpy as np

from q_analyze import load_ir

BANDS = [(20, 1000), (1000, 10000), (10000, 20000), (20000, 24000)]


def spectrum_weights(f, kind):
    w = np.zeros_like(f)
    ok = f >= 20.0
    if kind == "white":
        w[ok] = 1.0
    elif kind == "pink":
        w[ok] = 1.0 / f[ok]
    elif kind == "music":
        # A typical long-term spectrum of a finished mix: pink to 4 kHz, about 6 dB/oct above, and very little
        # above 20 kHz (an assumption standing in for program material; the pink and white cases bracket it).
        w[ok] = 1.0 / f[ok]
        hi = f > 4000.0
        w[hi] *= 4000.0 / f[hi]
        w[f > 20000.0] *= 0.01
    return w


def responses(ref, test, fs, n):
    Hr = np.fft.rfft(ref, n)
    Ht = np.fft.rfft(test, n)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    return f, Hr, Ht


def residual_db(f, Hr, Ht, w, mask=None):
    m = np.ones_like(f, bool) if mask is None else mask
    num = np.sum((np.abs(Ht - Hr) ** 2 * w)[m])
    den = np.sum((np.abs(Hr) ** 2 * w))
    return 10 * np.log10(num / den + 1e-300)


def parts_db(f, Hr, Ht, w, mask):
    mag = (np.abs(Ht) - np.abs(Hr)) ** 2
    ph = 2 * np.abs(Ht) * np.abs(Hr) * (1 - np.cos(np.angle(Ht / Hr)))
    den = np.sum(np.abs(Hr) ** 2 * w)
    return (10 * np.log10(np.sum((mag * w)[mask]) / den + 1e-300),
            10 * np.log10(np.sum((ph * w)[mask]) / den + 1e-300))


def time_domain_null(ref, test, fs, seconds=20.0, seed=3):
    rng = np.random.default_rng(seed)
    n = int(seconds * fs)
    x = rng.standard_normal(n)
    X = np.fft.rfft(x)
    fx = np.fft.rfftfreq(n, 1.0 / fs)
    X[1:] /= np.sqrt(fx[1:])
    X[fx < 20.0] = 0   # same band as the prediction (20 Hz up)
    x = np.fft.irfft(X, n)
    N = 1 << int(np.ceil(np.log2(n + len(ref))))
    yr = np.fft.irfft(np.fft.rfft(x, N) * np.fft.rfft(ref, N), N)[:n]
    yt = np.fft.irfft(np.fft.rfft(x, N) * np.fft.rfft(test, N), N)[:n]
    s = slice(int(0.5 * fs), n - int(0.5 * fs))
    d = yt[s] - yr[s]
    rms = 10 * np.log10(np.mean(d ** 2) / np.mean(yr[s] ** 2))
    peak = 20 * np.log10(np.max(np.abs(d)) / np.max(np.abs(yr[s])))
    return rms, peak


def main(ref_dir, ref_prefix, test_dir, test_prefix):
    pairs = []
    for rp in sorted(Path(ref_dir).glob(ref_prefix + " *.wav")):
        key = rp.stem[len(ref_prefix) + 1:]
        tp = Path(test_dir) / (test_prefix + " " + key + ".wav")
        if tp.exists():
            fs, r = load_ir(rp)
            fst, t = load_ir(tp)
            assert fs == fst, "renders at different rates"
            # load_ir starts 0.1 s before the impulse; both renders are aligned the same way.
            pairs.append((key, fs, r, t))

    print("### Predicted null depth (sample-aligned, gains matched)\n")
    print("| Setting | Pink noise | Music-like spectrum | White noise | Pink, time-domain check: RMS / peak |")
    print("|---|---|---|---|---|")
    for key, fs, r, t in pairs:
        n = 1 << 18
        f, Hr, Ht = responses(r, t, fs, n)
        vals = [residual_db(f, Hr, Ht, spectrum_weights(f, k)) for k in ("pink", "music", "white")]
        td = time_domain_null(r, t, fs)
        print("| %s | %.1f dB | %.1f dB | %.1f dB | %.1f / %.1f dB |" % (key, vals[0], vals[1], vals[2], td[0], td[1]))

    print("\n### Where the residual comes from (pink noise)\n")
    print("Each cell: magnitude part / phase part, in dB relative to the whole signal. The larger one dominates.\n")
    print("| Setting | " + " | ".join("%g-%g Hz" % b for b in BANDS) + " |")
    print("|---|" + "---|" * len(BANDS))
    for key, fs, r, t in pairs:
        n = 1 << 18
        f, Hr, Ht = responses(r, t, fs, n)
        w = spectrum_weights(f, "pink")
        cells = []
        for lo, hi in BANDS:
            m = (f >= lo) & (f < hi)
            a, b = parts_db(f, Hr, Ht, w, m)
            cells.append("%.0f / %.0f" % (a, b))
        print("| %s | %s |" % (key, " | ".join(cells)))

    print("\n### Group delay: COHERENCE minus the reference\n")
    print("The time shift that the phase difference amounts to, per frequency (positive: COHERENCE later).\n")
    print("| Setting | Largest difference, 20 Hz-1 kHz | 1-10 kHz | 10-20 kHz |")
    print("|---|---|---|---|")
    for key, fs, r, t in pairs:
        n = 1 << 18
        f, Hr, Ht = responses(r, t, fs, n)
        dphi = np.unwrap(np.angle(Ht / Hr))
        gd = -np.gradient(dphi, 2 * np.pi * f)   # seconds
        cells = []
        for lo, hi in ((20, 1000), (1000, 10000), (10000, 20000)):
            m = (f >= lo) & (f < hi)
            i = np.argmax(np.abs(gd[m]))
            cells.append("%+.1f us" % (gd[m][i] * 1e6))
        print("| %s | %s |" % (key, " | ".join(cells)))

    print("\n### How errors in the test itself would change a null (music-like spectrum)\n")
    deep = min(pairs, key=lambda p: residual_db(*responses(p[2], p[3], p[1], 1 << 18), spectrum_weights(responses(p[2], p[3], p[1], 1 << 18)[0], "music")))
    key, fs, r, t = deep
    n = 1 << 18
    f, Hr, Ht = responses(r, t, fs, n)
    w = spectrum_weights(f, "music")
    print("Starting from the deepest case, `%s` (%.1f dB):\n" % (key, residual_db(f, Hr, Ht, w)))
    print("| Error in the test | Null depth |")
    print("|---|---|")
    for lab, H in (("one plug-in 1 sample late", Ht * np.exp(-2j * np.pi * f / fs)),
                   ("polarity not inverted (the two add)", -Ht),
                   ("level 0.01 dB off", Ht * 10 ** (0.01 / 20)),
                   ("level 0.05 dB off", Ht * 10 ** (0.05 / 20)),
                   ("level 0.1 dB off", Ht * 10 ** (0.1 / 20))):
        print("| %s | %.1f dB |" % (lab, residual_db(f, Hr, H, w)))
    print("\nA level error on its own (identical filters) limits a null to: 0.01 dB -> %.1f dB, 0.05 dB -> %.1f dB, 0.1 dB -> %.1f dB."
          % tuple(20 * np.log10(10 ** (g / 20) - 1) for g in (0.01, 0.05, 0.1)))


if __name__ == "__main__":
    main(*sys.argv[1:5])
