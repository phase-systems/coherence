#!/usr/bin/env python3
"""Test signals for measuring a compressor (FabFilter Pro-C 3 as the reference, and COHERENCE against it; COMPRESSOR-TESTING.md).

Usage: python3 comp_signals.py <output folder> [sample rate, default 48000]

Writes one 32-bit float stereo WAV per test plus `manifest.json`, which records every segment (start, length,
signal, level) so that `comp_analyze.py` can measure the captures without guessing. Every file:
  * 0.0-0.5 s   silence
  * 0.5 s       sync click (one sample, 0.5 on both channels): finds the capture's latency to the sample
  * 1.0 s on    the test segments, separated by silence long enough for the slowest release to recover

Files (rendered at the settings in COMPRESSOR-TESTING.md section 2.1):
  C0 loopback       pink noise and a sine sweep, for the path with the compressor bypassed (null test, latency)
  C1 static         1 kHz sine staircase -60 .. 0 dBFS in 2 dB steps, up then down (static curve, hysteresis);
                    written mono (both channels would be identical)
  C2 detector       equal-RMS sine, square and noise; equal-peak sine and square; 30 Hz / 100 Hz / 1 kHz / 10 kHz
                    sines at one level; left-only, right-only, both, and opposite-polarity stereo (detector law,
                    sidechain frequency response, stereo link)
  C3 bursts         1 kHz tone held 10 dB under threshold, stepping +10 and +20 dB over it for 0.5 s, then back
                    (attack and release envelopes); bursts of 10 ms, 50 ms, 200 ms and 1 s (program dependence)
  C4 harmonics      50 Hz, 100 Hz and 1 kHz steady sines at three levels; SMPTE IMD pair (60 Hz + 7 kHz, 4:1)
Levels assume the threshold is set to -20 dBFS; the manifest stores them in dBFS (peak for tones).
"""
import json
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile

THRESHOLD = -20.0


def db(x):
    return 10 ** (x / 20.0)


class File:
    def __init__(self, name, fs):
        self.name, self.fs = name, fs
        self.parts = [np.zeros((int(0.5 * fs), 2))]
        click = np.zeros((int(0.5 * fs), 2))
        click[0, :] = 0.5
        self.parts.append(click)
        self.t = int(1.0 * fs)
        self.segments = []

    def add(self, sig, kind, gap=1.5, **info):
        """sig: (n, 2) array. A gap of silence follows (seconds)."""
        self.segments.append(dict(kind=kind, start=self.t, length=len(sig), **info))
        self.parts.append(sig)
        g = np.zeros((int(gap * self.fs), 2))
        self.parts.append(g)
        self.t += len(sig) + len(g)

    def write(self, folder, mono=False):
        x = np.concatenate(self.parts).astype(np.float32)
        if mono:   # identical channels: half the size; a DAW plays it on both channels
            x = x[:, 0]
        wavfile.write(str(folder / (self.name + ".wav")), self.fs, x)
        return dict(file=self.name + ".wav", fs=self.fs, sync=int(0.5 * self.fs), segments=self.segments)


def tone(fs, f, seconds, peak_db, fade=0.005):
    n = int(seconds * fs)
    t = np.arange(n) / fs
    x = db(peak_db) * np.sin(2 * np.pi * f * t)
    r = int(fade * fs)
    env = np.ones(n)
    env[:r] = np.sin(np.linspace(0, np.pi / 2, r)) ** 2
    env[-r:] = env[:r][::-1]
    return x * env


def stereo(x, right=None):
    return np.stack([x, x if right is None else right], 1)


def main(folder, fs):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(1)
    files = []

    # C0 loopback: pink noise and a log sweep.
    f = File("C0 loopback", fs)
    w = rng.standard_normal(int(4 * fs))
    spec = np.fft.rfft(w)
    spec[1:] /= np.sqrt(np.arange(1, len(spec)))
    pink = np.fft.irfft(spec, len(w))
    pink *= db(-20) / np.sqrt(np.mean(pink ** 2))
    f.add(stereo(pink), "pink", rms_db=-20.0)
    n = int(6 * fs)
    t = np.arange(n) / fs
    k = np.log(20000 / 20)
    sweep = db(-12) * np.sin(2 * np.pi * 20 * 6 / k * (np.exp(t / 6 * k) - 1))
    f.add(stereo(sweep), "sweep", peak_db=-12.0, f_lo=20.0, f_hi=20000.0)
    files.append(f.write(folder))

    # C1 static curve: one continuous staircase so the detector stays settled between steps.
    f = File("C1 static", fs)
    levels = list(np.arange(-60, 1, 2)) + list(np.arange(-2, -61, -2))
    step = int(1.2 * fs)
    sig = np.concatenate([db(L) * np.sin(2 * np.pi * 1000 * (np.arange(step) + i * step) / fs) for i, L in enumerate(levels)])
    f.add(stereo(sig), "staircase", f=1000.0, levels=[float(L) for L in levels], step=step, measure_from=0.6)
    files.append(f.write(folder, mono=True))

    # C2 detector law, sidechain frequency response, stereo link.
    f = File("C2 detector", fs)
    over = THRESHOLD + 10.0
    s = tone(fs, 1000, 2.0, over)
    f.add(stereo(s), "sine", rms_db=over - 3.01, peak_db=over)
    sq = np.sign(np.sin(2 * np.pi * 1000 * np.arange(int(2 * fs)) / fs)) * db(over - 3.01)
    f.add(stereo(sq), "square_equal_rms", rms_db=over - 3.01, peak_db=over - 3.01)
    sq2 = np.sign(np.sin(2 * np.pi * 1000 * np.arange(int(2 * fs)) / fs)) * db(over)
    f.add(stereo(sq2), "square_equal_peak", rms_db=over, peak_db=over)
    nz = rng.standard_normal(int(2 * fs))
    nz *= db(over - 3.01) / np.sqrt(np.mean(nz ** 2))
    f.add(stereo(nz), "noise_equal_rms", rms_db=over - 3.01)
    for fr in (30.0, 100.0, 1000.0, 10000.0):
        f.add(stereo(tone(fs, fr, 2.0, over)), "sine_freq", f=fr, peak_db=over)
    s = tone(fs, 1000, 2.0, over)
    f.add(stereo(s, np.zeros_like(s)), "left_only", peak_db=over)
    f.add(stereo(np.zeros_like(s), s), "right_only", peak_db=over)
    f.add(stereo(s, -s), "opposite_polarity", peak_db=over)
    f.add(stereo(s, db(-20) * s), "left_loud_right_quiet", peak_db=over, right_peak_db=over - 20.0)
    files.append(f.write(folder))

    # C3 bursts: steps over threshold from a bed 10 dB under it.
    f = File("C3 bursts", fs, )
    bed = THRESHOLD - 10.0
    for over_db in (10.0, 20.0):
        n_pre, n_on, n_post = int(1.0 * fs), int(0.5 * fs), int(2.5 * fs)
        n_all = n_pre + n_on + n_post
        env = np.full(n_all, db(bed))
        env[n_pre:n_pre + n_on] = db(THRESHOLD + over_db)
        x = env * np.sin(2 * np.pi * 1000 * np.arange(n_all) / fs)
        f.add(stereo(x), "step", f=1000.0, bed_db=bed, burst_db=THRESHOLD + over_db, burst_start=n_pre, burst_length=n_on)
    for dur in (0.01, 0.05, 0.2, 1.0):
        n_pre, n_on, n_post = int(1.0 * fs), int(dur * fs), int(2.5 * fs)
        n_all = n_pre + n_on + n_post
        env = np.full(n_all, db(bed))
        env[n_pre:n_pre + n_on] = db(THRESHOLD + 15.0)
        x = env * np.sin(2 * np.pi * 1000 * np.arange(n_all) / fs)
        f.add(stereo(x), "burst", f=1000.0, bed_db=bed, burst_db=THRESHOLD + 15.0, burst_start=n_pre, burst_length=n_on)
    files.append(f.write(folder))

    # C4 harmonics and IMD.
    f = File("C4 harmonics", fs)
    for fr in (50.0, 100.0, 1000.0):
        for lvl in (THRESHOLD, THRESHOLD + 8.0, THRESHOLD + 16.0):
            f.add(stereo(tone(fs, fr, 3.0, lvl)), "harmonic", f=fr, peak_db=lvl, measure_from=1.5)
    n = int(3 * fs)
    t = np.arange(n) / fs
    imd = 0.8 * np.sin(2 * np.pi * 60 * t) + 0.2 * np.sin(2 * np.pi * 7000 * t)
    imd *= db(THRESHOLD + 10.0)
    f.add(stereo(imd), "smpte_imd", f_low=60.0, f_high=7000.0, peak_db=THRESHOLD + 10.0, measure_from=1.5)
    files.append(f.write(folder))

    json.dump(dict(threshold_db=THRESHOLD, files=files), open(folder / "manifest.json", "w"), indent=1)
    print("wrote", len(files), "files to", folder)


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 48000)
