#!/usr/bin/env python3
"""Measure bell filters from impulse-response renders and work out how each EQ defines Q.

Usage:  python3 q_analyze.py <folder with renders> [more folders...]

File names: "<EQ> <freq> Q<q> <gain>.wav", e.g. "MDW 1k Q1 +12.wav", "WEISS 10k Q1 +6.wav",
"COH 1k Q4 +6.wav". The test file is a 0.5-amplitude impulse at 1.0 s, 48 kHz.

For every render the script fits a second-order analog bell
    H(s) = (s^2 + s*w0/Qz + w0^2) / (s^2 + s*w0/Qp + w0^2)
to the measured magnitude (20 Hz .. 16 kHz) and reports, next to the Q set on the plug-in:
  - the pole Q (the "classic EE" Q of the resonance; for a boost this is the band-pass Q),
  - the half-gain Q, sqrt(Qp*Qz): the Q defined between the half-gain (dB) points, which is how
    COHERENCE v0.1 (RBJ constant-Q) defines it,
  - the measured bandwidths (octaves) between the half-gain points and 3 dB below the peak.
Comparing those columns with the set Q shows which convention the EQ follows and whether the
relationship is constant across gain, frequency and Q.
"""
import re
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.optimize import least_squares

FS_EXPECTED = 48000
NAME = re.compile(r"^(?P<eq>\S+)\s+(?P<f>[\d.]+)(?P<k>k?)\s+Q(?P<q>[\d.]+)\s+(?P<g>[+-]?[\d.]+)", re.I)


def load_ir(path):
    fs, x = wavfile.read(path)
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x[:, 0]
    if x.dtype.kind == "i":
        x /= np.iinfo(np.int32).max
    # Window around the impulse (at 1.0 s), allowing for plug-in latency and pre-ringing.
    start = int(0.9 * fs)
    seg = x[start:start + int(1.9 * fs)]
    return fs, seg / 0.5


def magnitude_db(ir, fs, freqs):
    n = 1 << int(np.ceil(np.log2(len(ir) * 2)))
    spec = np.fft.rfft(ir, n)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    mag = np.interp(freqs, f, np.abs(spec))
    return 20.0 * np.log10(mag + 1e-30)


def bell_db(freqs, f0, qp, qz):
    w = freqs / f0
    num = (1.0 - w * w) ** 2 + (w / qz) ** 2
    den = (1.0 - w * w) ** 2 + (w / qp) ** 2
    return 10.0 * np.log10(num / den)


def fit_bell(freqs, db, f0_guess, gain_guess, q_guess):
    a = 10 ** (gain_guess / 40.0)
    x0 = [np.log(f0_guess), np.log(q_guess * a), np.log(q_guess / a)]
    res = least_squares(lambda p: bell_db(freqs, np.exp(p[0]), np.exp(p[1]), np.exp(p[2])) - db, x0)
    f0, qp, qz = np.exp(res.x)
    return f0, qp, qz, np.max(np.abs(res.fun))


def bandwidth_octaves(freqs, db, level):
    """Octaves between the two crossings of `level` around the extremum."""
    peak = np.argmax(np.abs(db))
    sign = np.sign(db[peak])
    above = sign * db >= sign * level
    lo = peak
    while lo > 0 and above[lo]:
        lo -= 1
    hi = peak
    while hi < len(db) - 1 and above[hi]:
        hi += 1
    if lo == 0 or hi == len(db) - 1:
        return float("nan")
    return np.log2(freqs[hi] / freqs[lo])


def q_from_octaves(n):
    return np.sqrt(2.0 ** n) / (2.0 ** n - 1.0)


def analyse(path):
    m = NAME.match(path.stem)
    if not m:
        return None
    f_set = float(m["f"]) * (1000.0 if m["k"] else 1.0)
    q_set = float(m["q"])
    g_set = float(m["g"])
    fs, ir = load_ir(path)
    freqs = np.geomspace(20.0, min(16000.0, 0.45 * fs), 2000)
    db = magnitude_db(ir, fs, freqs)
    f0, qp, qz, fit_err = fit_bell(freqs, db, f_set, g_set, q_set)
    peak_db = 20.0 * np.log10(qp / qz)
    if peak_db < 0:   # a cut: the "classic" Q is the Q of the notch-side resonance (the zeros)
        classic_q = qz
    else:
        classic_q = qp
    half_gain_q = np.sqrt(qp * qz)
    bw_half = bandwidth_octaves(freqs, db, db[np.argmax(np.abs(db))] / 2.0)
    bw_3db = bandwidth_octaves(freqs, db, db[np.argmax(np.abs(db))] - 3.0 * np.sign(peak_db)) if abs(peak_db) > 3.5 else float("nan")
    return dict(eq=m["eq"].upper(), file=path.name, f_set=f_set, q_set=q_set, g_set=g_set, fs=fs,
                f0=f0, peak_db=peak_db, qp=qp, qz=qz, classic_q=classic_q, half_gain_q=half_gain_q,
                bw_half=bw_half, q_half_measured=q_from_octaves(bw_half) if np.isfinite(bw_half) else float("nan"),
                bw_3db=bw_3db, fit_err=fit_err)


def main(folders):
    rows = []
    for folder in folders:
        for p in sorted(Path(folder).glob("*.wav")):
            r = analyse(p)
            if r:
                rows.append(r)
    if not rows:
        print("no renders found")
        return
    hdr = "| EQ | Setting | Fitted f0 | Peak | Pole Q (classic) | Half-gain Q | Classic / set | Half-gain / set | BW at half gain | BW at -3 dB from peak | Fit residual |"
    print(hdr)
    print("|---" * (hdr.count("|") - 1) + "|")
    for r in rows:
        print("| %s | %s Hz, Q %.2f, %+.1f dB | %.1f Hz | %+.2f dB | %.3f | %.3f | %.3f | %.3f | %.3f oct | %s | %.3f dB |" % (
            r["eq"], ("%g" % r["f_set"]), r["q_set"], r["g_set"], r["f0"], r["peak_db"], r["classic_q"],
            r["half_gain_q"], r["classic_q"] / r["q_set"], r["half_gain_q"] / r["q_set"], r["bw_half"],
            ("%.3f oct" % r["bw_3db"]) if np.isfinite(r["bw_3db"]) else "-", r["fit_err"]))


if __name__ == "__main__":
    main(sys.argv[1:] or ["."])
