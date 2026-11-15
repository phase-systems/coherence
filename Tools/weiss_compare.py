#!/usr/bin/env python3
"""Compare COHERENCE's EQ modes with Weiss EQ MP and MDWEQ6 renders (impulse 0.5 at 1.0 s, 48 kHz, 32-bit float).

Usage: python3 weiss_compare.py <renders folder> <CoherenceTests binary> <work folder>

Render names understood (as in REFERENCES/EQ renders v2/test results):
    WEISS [48K] <BELL|HS|LS> <freq> <Q> <gain>   e.g. "WEISS BELL 10000 1 +6", "WEISS HS 10K 0.71 -6"
    WEISS 48K <freq> <Q> <gain>                  low shelf (the round-2 low-shelf series)
    MDW 48K <BELL|HS|LS|HP|LP> <freq> [<Q>] [<gain>]
The 96 kHz renders are skipped: the 96K BYPASS render shows the impulse was resampled, so they are not valid.

For each render: COHERENCE is rendered with the same settings (Weiss mode, and the normal mode), with host delay
compensation, and compared on magnitude, phase and the impulse itself.
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile


def load(p):
    fs, x = wavfile.read(p)
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x[:, 0]
    return fs, x / 0.5


def parse(stem):
    t = stem.replace("1OO", "100").replace("3OO", "300").replace("-A", " A").split()
    who, t = t[0], t[1:]
    if t and t[0] in ("48K", "96K"):
        if t[0] == "96K":
            return None
        t = t[1:]
    if not t:
        return None
    if t[0] not in ("BELL", "HS", "LS", "HP", "LP"):
        if who == "WEISS" and len(t) == 3:
            t = ["LS"] + t
        else:
            return None
    kind = t[0]
    f = t[1].upper()
    f0 = float(f[:-1]) * 1000 if f.endswith("K") else float(f)
    if kind in ("HP", "LP"):
        return who, kind, f0, 0.7071, 0.0
    if who == "MDW" and kind in ("HS", "LS"):
        return who, kind, f0, 0.7071, float(t[2])
    return who, kind, f0, float(t[2]), float(t[3])


def spec_name(kind, f0, q, g):
    return f"{kind} {f0:g} {q:g} {g:+g}"


def compare(ref, coh, fs):
    n = min(len(ref), len(coh))
    i0 = fs
    a, b = ref[i0 - 4800:i0 + int(1.5 * fs)], coh[i0 - 4800:i0 + int(1.5 * fs)]
    A, B = np.fft.rfft(np.roll(a, -4800)), np.fft.rfft(np.roll(b, -4800))
    f = np.fft.rfftfreq(len(a), 1 / fs)
    lo, hi = (f >= 20) & (f <= 10000), (f > 10000) & (f <= 20000)
    d = 20 * np.log10(np.abs(B) / np.abs(A))
    ph = np.degrees(np.angle(B / A))
    err = b - a
    null = 10 * np.log10(np.sum(err ** 2) / np.sum(a ** 2))
    return np.max(np.abs(d[lo])), np.max(np.abs(d[hi])), np.max(np.abs(ph[(f >= 20) & (f <= 20000)])), np.max(np.abs(err)), null


def pre_ring(x, fs):
    pre = x[fs - 400:fs]
    nz = np.nonzero(np.abs(pre) > 1e-6)[0]
    return (len(pre) - nz[0]) if len(nz) else 0, float(np.sum(pre ** 2))


def main():
    renders, binary, work = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    work.mkdir(parents=True, exist_ok=True)
    items = []
    for p in sorted(renders.glob("*.wav")):
        r = parse(p.stem)
        if r:
            items.append((p, *r))
    specs = sorted({spec_name(k, f0, q, g) for _, _, k, f0, q, g in items})
    for mode in ("weiss", "normal"):
        subprocess.run([binary, "--render-mode", str(work), mode, ";".join(specs)], check=True, capture_output=True)
    print("| Reference | COHERENCE mode | Magnitude diff 20 Hz-10 kHz / 10-20 kHz | Phase diff to 20 kHz | Impulse: max error / null depth | Pre-ringing (samples before, reference / COHERENCE) |")
    print("|---|---|---|---|---|---|")
    for p, who, kind, f0, q, g in items:
        fs, ref = load(p)
        for mode, prefix in (("Weiss", "COH-WEISS"), ("normal", "COH-ENGINE")):
            if who == "WEISS" and mode == "normal":
                continue
            fs2, coh = load(work / f"{prefix} {spec_name(kind, f0, q, g)}.wav")
            m1, m2, ph, e, nl = compare(ref, coh, fs)
            pr, pc = pre_ring(ref, fs), pre_ring(coh, fs)
            print(f"| {p.stem} | {mode} | {m1:.4f} / {m2:.4f} dB | {ph:.3f} deg | {e:.1e} / {nl:.0f} dB | {pr[0]} / {pc[0]} |")


if __name__ == "__main__":
    main()
