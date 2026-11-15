#!/usr/bin/env python3
"""Compare two sets of impulse-response renders setting by setting (e.g. MDWEQ6 vs COHERENCE).

Usage: python3 compare_renders.py <reference folder> <reference prefix> <test folder> <test prefix>
Files are matched by the part of the name after the prefix ("1k Q1 +12" etc.).

Magnitude difference, and the null depth: the level of the difference signal (A - B) relative to the signal,
per frequency, which is what a sample-aligned null test of the two plug-ins would leave. It includes phase
differences (1 degree alone leaves -35 dB). Both renders must have the same latency (here both are 0).
"""
import sys
from pathlib import Path
import numpy as np
from q_analyze import load_ir, magnitude_db

def complex_response(ir, fs, freqs):
    n = 1 << int(np.ceil(np.log2(len(ir) * 2)))
    spec = np.fft.rfft(ir, n)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    return np.interp(freqs, f, spec.real) + 1j * np.interp(freqs, f, spec.imag)


def main(ref_dir, ref_prefix, test_dir, test_prefix):
    freqs = np.geomspace(20.0, 20000.0, 3000)
    print("| Setting | Max difference 20 Hz-16 kHz | Max difference 16-20 kHz | RMS difference 20 Hz-20 kHz | Largest at | Null depth 20 Hz-1 kHz / 1-10 kHz / 10-20 kHz (shallowest) |")
    print("|---|---|---|---|---|---|")
    for rp in sorted(Path(ref_dir).glob(ref_prefix + " *.wav")):
        key = rp.stem[len(ref_prefix) + 1:]
        tp = Path(test_dir) / (test_prefix + " " + key + ".wav")
        if not tp.exists():
            continue
        fs, a = load_ir(rp)
        fsb, b = load_ir(tp)   # the test may be rendered at another rate (same 1.0 s impulse time)
        d = magnitude_db(b, fsb, freqs) - magnitude_db(a, fs, freqs)
        lo = freqs <= 16000
        i = np.argmax(np.abs(d))
        ca, cb = complex_response(a, fs, freqs), complex_response(b, fsb, freqs)
        null = 20 * np.log10(np.abs(cb - ca) / np.abs(ca) + 1e-30)
        bands = [freqs <= 1000, (freqs > 1000) & (freqs <= 10000), freqs > 10000]
        print("| %s | %.3f dB | %.3f dB | %.3f dB | %.0f Hz (%+.3f dB) | %s |" % (
            key, np.max(np.abs(d[lo])), np.max(np.abs(d[~lo])), np.sqrt(np.mean(d ** 2)), freqs[i], d[i],
            " / ".join("%.0f dB" % np.max(null[m]) for m in bands)))

if __name__ == "__main__":
    main(*sys.argv[1:5])
