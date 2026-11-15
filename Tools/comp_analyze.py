#!/usr/bin/env python3
"""Measure compressor captures made with the comp_signals.py test files.

Usage: python3 comp_analyze.py <signals folder> <capture.wav> [<capture.wav> ...]

A capture is the test file recorded back after the compressor, named "<test file name> <setting>.wav", for
example "C1 static R4 K0.wav" or "C3 bursts A10 R150.wav". The test file is found from the name; the capture
is aligned with the sync click, so any latency is measured and removed.

Reports (Markdown), per capture:
  latency, and for C0 the null against the source (the path with the compressor bypassed should null);
  C1  input level -> gain, up and down the staircase (static curve, hysteresis);
  C2  steady gain for each detector test (law, sidechain frequency response, stereo link: left/right gains);
  C3  attack and release: time to 63 % and 90 % of the final change in gain (dB), per step and burst;
  C4  THD (harmonics 2-9), strongest harmonic, and SMPTE IMD.
Gains are output/input in dB at matching moments, so makeup gain appears as an offset.
"""
import json
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile


def read(path):
    fs, x = wavfile.read(str(path))
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], 1)
    if np.issubdtype(np.asarray(wavfile.read(str(path))[1]).dtype, np.integer):
        x /= 2 ** 31
    return fs, x


def rms_db(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-30)


def amp(x, f, fs):
    n = np.arange(len(x))
    return 2 * np.abs(np.sum(x * np.exp(-2j * np.pi * f * n / fs))) / len(x)


def whole_cycles(n, f, fs):
    per = fs / f
    return int(np.floor(n / per) * per)


def gain_envelope(x, y, fs, win=0.001):
    """Gain in dB over time from short RMS windows (1 ms = whole cycles of 1 kHz)."""
    w = int(win * fs)
    m = len(x) // w
    xi = np.sqrt(np.mean(x[: m * w].reshape(m, w) ** 2, 1)) + 1e-30
    yo = np.sqrt(np.mean(y[: m * w].reshape(m, w) ** 2, 1)) + 1e-30
    return 20 * np.log10(yo / xi), w


def settle_time(g, start, target_from, target_to, frac, w, fs):
    """Time (ms) after `start` (window index) until the gain has made `frac` of the change from->to."""
    level = target_from + frac * (target_to - target_from)
    seg = g[start:]
    if target_to < target_from:
        idx = np.argmax(seg <= level)
        ok = seg[idx] <= level
    else:
        idx = np.argmax(seg >= level)
        ok = seg[idx] >= level
    return idx * w / fs * 1000.0 if ok else float("nan")


def analyse(sig_folder, cap_path, manifest):
    name = Path(cap_path).stem
    entry = next((f for f in manifest["files"] if name.startswith(f["file"][:-4])), None)
    if entry is None:
        print("\n%s: no matching test file" % name)
        return
    fs, src = read(Path(sig_folder) / entry["file"])
    cfs, cap = read(cap_path)
    assert cfs == fs, "capture rate %d differs from the test file's %d" % (cfs, fs)
    sync = entry["sync"]
    found = int(np.argmax(np.abs(cap[: int(0.95 * fs), 0])))
    lat = found - sync
    cap = np.roll(cap, -lat, axis=0) if lat else cap
    print("\n### %s\n\nLatency: %d samples (sync click found at %d).\n" % (name, lat, found))
    segs = entry["segments"]

    if name.startswith("C0"):
        print("| Segment | Null against the source (residual / signal) |\n|---|---|")
        for s in segs:
            a, b = s["start"], s["start"] + s["length"]
            for c in range(2):
                r = rms_db(cap[a:b, c] - src[a:b, c]) - rms_db(src[a:b, c])
                print("| %s ch%d | %.1f dB |" % (s["kind"], c + 1, r))

    if name.startswith("C1"):
        s = segs[0]
        step = s["step"]
        print("| Input (dBFS peak) | Gain going up | Gain coming down | Output going up (dBFS peak) |\n|---|---|---|---|")
        lv = s["levels"]
        half = len(lv) // 2 + 1
        res = {}
        for i, L in enumerate(lv):
            a = s["start"] + i * step + int(s["measure_from"] * step)
            b = s["start"] + (i + 1) * step
            g = rms_db(cap[a:b, 0]) - rms_db(src[a:b, 0])
            res.setdefault(L, []).append(g)
        for L in sorted(res):
            up = res[L][0]
            down = res[L][1] if len(res[L]) > 1 else float("nan")
            print("| %.0f | %.2f dB | %.2f dB | %.2f |" % (L, up, down, L + up))

    if name.startswith("C2"):
        print("| Test | Gain left | Gain right |\n|---|---|---|")
        for s in segs:
            a = s["start"] + s["length"] // 2
            b = s["start"] + s["length"] - int(0.01 * fs)
            gl = rms_db(cap[a:b, 0]) - rms_db(src[a:b, 0]) if np.any(src[a:b, 0]) else float("nan")
            gr = rms_db(cap[a:b, 1]) - rms_db(src[a:b, 1]) if np.any(src[a:b, 1]) else float("nan")
            label = s["kind"] + (" %g Hz" % s["f"] if "f" in s else "")
            print("| %s | %.2f dB | %.2f dB |" % (label, gl, gr))

    if name.startswith("C3"):
        print("| Event | Gain before | Gain at end of burst | Attack 63 % / 90 % | Release 63 % / 90 % |\n|---|---|---|---|---|")
        for s in segs:
            a = s["start"]
            x, y = src[a:a + s["length"], 0], cap[a:a + s["length"], 0]
            g, w = gain_envelope(x, y, fs)
            on, off = s["burst_start"] // w, (s["burst_start"] + s["burst_length"]) // w
            g_before = np.median(g[max(0, on - 300):on - 5])
            g_end = g[off - 1]
            att = [settle_time(g, on, g_before, g_end, fr, w, fs) for fr in (0.632, 0.9)]
            rel = [settle_time(g, off, g_end, g_before, fr, w, fs) for fr in (0.632, 0.9)]
            label = "%s %.0f ms at %+.0f dB over" % (s["kind"], s["burst_length"] / fs * 1000, s["burst_db"] - manifest["threshold_db"])
            print("| %s | %.2f dB | %.2f dB | %.1f / %.1f ms | %.1f / %.1f ms |" % (label, g_before, g_end, att[0], att[1], rel[0], rel[1]))

    if name.startswith("C4"):
        print("| Tone | Gain | THD (2-9) | Strongest |\n|---|---|---|---|")
        for s in segs:
            a = s["start"] + int(s["measure_from"] * fs)
            n = s["length"] - int(s["measure_from"] * fs) - int(0.01 * fs)
            if s["kind"] == "smpte_imd":
                n = whole_cycles(n, 60.0, fs)
                y = cap[a:a + n, 0]
                hi = amp(y, s["f_high"], fs)
                side = np.sqrt(sum(amp(y, s["f_high"] + k * s["f_low"], fs) ** 2 for k in (-3, -2, -1, 1, 2, 3)))
                print("| SMPTE IMD 60 Hz + 7 kHz | | %.1f dB | |" % (20 * np.log10(side / hi + 1e-30)))
                continue
            f0 = s["f"]
            n = whole_cycles(n, f0, fs)
            y, x = cap[a:a + n, 0], src[a:a + n, 0]
            h1 = amp(y, f0, fs)
            hs = np.array([amp(y, k * f0, fs) for k in range(2, 10)])
            thd = 10 * np.log10(np.sum(hs ** 2) / h1 ** 2 + 1e-30)
            print("| %g Hz at %.0f dBFS | %.2f dB | %.1f dB (%.3f %%) | H%d |" % (
                f0, s["peak_db"], 20 * np.log10(h1 / amp(x, f0, fs)), thd, 100 * np.sqrt(np.sum(hs ** 2)) / h1, 2 + int(np.argmax(hs))))


def main(sig_folder, caps):
    manifest = json.load(open(Path(sig_folder) / "manifest.json"))
    for c in caps:
        analyse(sig_folder, c, manifest)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
