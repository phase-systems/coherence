#!/usr/bin/env python3
"""Measure reference EQ renders (MDWEQ6, Weiss EQ MP, ...) against analog prototypes and against COHERENCE.

Usage: python3 reference_compare.py <renders folder> <prefix> [<COHERENCE renders folder> <COHERENCE prefix>]

Render names (see REFERENCES/EQ renders v2/HOW TO RENDER.md):
    <prefix> BELL 3k Q2 +6.wav      bell / peak
    <prefix> LS 100 +6.wav          low shelf at the plug-in's default shape
    <prefix> HS 10k -6.wav          high shelf
    <prefix> HP 40.wav              low cut (high-pass)
    <prefix> LP 8k.wav              high cut (low-pass)
    <prefix> MULTI A.wav            the four-band setting
    ... optional " 96k" at the end for renders made at 96 kHz.
    The v0.2 names ("MDW 1k Q1 +12.wav") are read as bells.

Every file is a 0.5 impulse at 1.0 s through the plug-in, 32-bit float.

What it reports, per render:
  * latency (the pure delay that best explains the phase);
  * the analog prototype that best fits the measured magnitude (least squares in dB, 20 Hz - 16 kHz): its
    frequency, Q and gain. For shelves and cuts this is how the plug-in defines its controls, which need not
    match COHERENCE's; nothing is assumed;
  * how far the render is from that fitted prototype in magnitude (and phase, once latency is removed);
  * with a COHERENCE folder: the magnitude difference and null depth against the COHERENCE render of the same
    name, where one exists.
Measured facts only; interpretation goes in DESIGN.md.
"""
import re
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

from q_analyze import load_ir

MULTI = {   # the four-band setting, in COHERENCE's definitions (shelves at Q 0.707)
    "A": [("LS", 120.0, 0.707, 3.5), ("BELL", 300.0, 1.4, -4.0), ("BELL", 450.0, 0.8, 2.5), ("HS", 8000.0, 0.707, 1.5)],
}


def analog(kind, f, f0, q, g):
    """Analog prototypes: bells in the classic Q definition (cut = inverse of boost), shelves and cuts as in
    COHERENCE (RBJ-style analog shelves, second-order cuts)."""
    s = 2j * np.pi * f
    w = 2 * np.pi * f0
    if kind == "BELL":
        G = 10 ** (abs(g) / 20.0)
        h = (s * s + s * G * w / q + w * w) / (s * s + s * w / q + w * w)
        return h if g >= 0 else 1.0 / h
    A = 10 ** (g / 40.0)
    sA = np.sqrt(A)
    if kind == "LS":
        return A * (s * s + sA * w / q * s + A * w * w) / (A * s * s + sA * w / q * s + w * w)
    if kind == "HS":
        return A * (A * s * s + sA * w / q * s + w * w) / (s * s + sA * w / q * s + A * w * w)
    if kind == "HP":
        return s * s / (s * s + w / q * s + w * w)
    if kind == "LP":
        return w * w / (s * s + w / q * s + w * w)
    raise ValueError(kind)


def parse(stem, prefix):
    rest = stem[len(prefix):].strip()
    rate = None
    if rest.endswith(" 96k"):
        rest, rate = rest[:-4], 96000
    m = re.match(r"MULTI (\w+)$", rest)
    if m:
        return dict(kind="MULTI", name=m.group(1), rate=rate)
    m = re.match(r"(?:(BELL|LS|HS|HP|LP) )?([\d.]+)(k?)(?: Q([\d.]+))?(?: ([+-][\d.]+))?$", rest)
    if not m:
        return None
    kind = m.group(1) or "BELL"
    f0 = float(m.group(2)) * (1000.0 if m.group(3) else 1.0)
    q = float(m.group(4)) if m.group(4) else (1.0 if kind == "BELL" else 0.707)
    g = float(m.group(5)) if m.group(5) else 0.0
    return dict(kind=kind, f0=f0, q=q, g=g, rate=rate)


def complex_response(ir, fs, freqs, offset):
    n = np.arange(len(ir)) - offset
    return np.array([np.sum(ir * np.exp(-2j * np.pi * f * n / fs)) for f in freqs])


def latency(h, ref, freqs, fs):
    dphi = np.unwrap(np.angle(h / ref))
    lo = freqs < 2000.0
    w = 2 * np.pi * freqs[lo] / fs
    d = -np.sum(w * dphi[lo]) / np.sum(w * w)
    return float(round(d)) if abs(d - round(d)) < 0.5 else d


def fit(kind, freqs, mag_db, f0, q, g):
    lo = freqs <= 16000.0
    has_gain = kind in ("BELL", "LS", "HS")

    def model(p):
        gg = p[2] if has_gain else 0.0
        return 20 * np.log10(np.abs(analog(kind, freqs, np.exp(p[0]), np.exp(p[1]), gg)))

    p0 = [np.log(f0), np.log(q)] + ([g] if has_gain else [])
    r = least_squares(lambda p: (model(p) - mag_db)[lo], p0)
    p = r.x
    return np.exp(p[0]), np.exp(p[1]), (p[2] if has_gain else 0.0), model(p)


def main(folder, prefix, coh_folder=None, coh_prefix=None):
    freqs = np.geomspace(20.0, 20000.0, 300)
    print("| Render | Latency | Fitted analog prototype (frequency, Q, gain) | Set on the plug-in | "
          "Max deviation from the fit, 20 Hz-16 kHz / 16-20 kHz | Phase vs the fit, to 10 kHz |")
    print("|---|---|---|---|---|---|")
    rows = []
    for p in sorted(Path(folder).glob(prefix + " *.wav")):
        spec = parse(p.stem, prefix)
        if spec is None:
            print("| %s | (name not recognised) |||||" % p.stem)
            continue
        fs, ir = load_ir(p)
        off = int(0.1 * fs)
        ir = ir[: int(0.7 * fs)]
        h = complex_response(ir, fs, freqs, off)
        mag = 20 * np.log10(np.abs(h) + 1e-30)
        rows.append((p.stem, spec, fs, h))
        if spec["kind"] == "MULTI":
            ref = np.ones_like(h)
            for kind, f0, q, g in MULTI[spec["name"]]:
                ref = ref * analog(kind, freqs, f0, q, g)
            lat = latency(h, ref, freqs, fs)
            dev = mag - 20 * np.log10(np.abs(ref))
            lo = freqs <= 16000
            ph = np.degrees(np.angle(h / ref * np.exp(2j * np.pi * freqs * lat / fs)))
            print("| %s | %g samples | (fixed: the four bands in COHERENCE's definitions) | MULTI %s | %.3f / %.3f dB | %.2f deg |"
                  % (p.stem, lat, spec["name"], np.max(np.abs(dev[lo])), np.max(np.abs(dev[~lo])), np.max(np.abs(ph[freqs <= 10000]))))
            continue
        kind = spec["kind"]
        f0, q, g, model = fit(kind, freqs, mag, spec["f0"], spec["q"], spec["g"])
        ref = analog(kind, freqs, f0, q, g)
        lat = latency(h, ref, freqs, fs)
        ph = np.degrees(np.angle(h / ref * np.exp(2j * np.pi * freqs * lat / fs)))
        keep = np.abs(ref) > 1e-3
        dev = mag - model
        lo = freqs <= 16000
        print("| %s | %g samples | %.1f Hz, Q %.3f, %+.2f dB | %g Hz, Q %g, %+g dB | %.3f / %.3f dB | %.2f deg |"
              % (p.stem, lat, f0, q, g, spec["f0"], spec["q"], spec["g"], np.max(np.abs(dev[lo & keep])),
                 np.max(np.abs(dev[~lo & keep])) if (~lo & keep).any() else 0.0, np.max(np.abs(ph[(freqs <= 10000) & keep]))))

    if coh_folder:
        print("\n| Setting | Magnitude difference 20 Hz-16 kHz / 16-20 kHz | Null depth 20 Hz-1 kHz / 1-10 kHz / 10-20 kHz |")
        print("|---|---|---|")
        for stem, spec, fs, h in rows:
            key = stem[len(prefix) + 1:]
            cp = Path(coh_folder) / (coh_prefix + " " + key + ".wav")
            if not cp.exists():
                continue
            cfs, cir = load_ir(cp)
            hc = complex_response(cir[: int(0.7 * cfs)], cfs, freqs, int(0.1 * cfs))
            d = 20 * np.log10(np.abs(hc) / np.abs(h))
            null = 20 * np.log10(np.abs(hc - h) / np.abs(h) + 1e-30)
            lo = freqs <= 16000
            bands = [freqs <= 1000, (freqs > 1000) & (freqs <= 10000), freqs > 10000]
            print("| %s | %.3f / %.3f dB | %s |" % (key, np.max(np.abs(d[lo])), np.max(np.abs(d[~lo])),
                                                     " / ".join("%.0f dB" % np.max(null[m]) for m in bands)))


if __name__ == "__main__":
    main(*sys.argv[1:5])
