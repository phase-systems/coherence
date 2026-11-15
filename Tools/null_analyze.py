#!/usr/bin/env python3
"""Analyse a real null test: two renders of the same source through two plug-ins (e.g. COHERENCE and MDWEQ6).

Usage: python3 null_analyze.py <reference.wav> <test.wav> [--channel 0]

Both files must be renders of the same source at the same sample rate, ideally exported from the same Ableton
set with the same start and length. The script measures, without assuming the test was set up correctly:
  1. alignment: the offset between the files (integer and fractional samples) and the polarity;
  2. level: the gain that best matches the test to the reference;
  3. the null as rendered (no corrections) and after removing any offset and level error, as RMS and peak;
  4. the transfer function between the two (Welch cross-spectrum): magnitude difference (dB) and phase
     difference (degrees) per band, with coherence;
  5. where the residual comes from, per band: level offset, magnitude-shape difference, or phase difference.
"""
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import csd, welch

BANDS = [(20, 200), (200, 1000), (1000, 5000), (5000, 10000), (10000, 16000), (16000, 20000), (20000, 24000)]


def read(path, ch):
    fs, x = wavfile.read(path)
    x = np.asarray(x, dtype=np.float64)
    if np.issubdtype(wavfile.read(path)[1].dtype, np.integer):
        x /= float(np.iinfo(wavfile.read(path)[1].dtype).max)
    if x.ndim > 1:
        x = x[:, ch]
    return fs, x


def db(x):
    return 10 * np.log10(x + 1e-300)


def main(ref_path, test_path, ch=0):
    fs, a = read(ref_path, ch)
    fst, b = read(test_path, ch)
    assert fs == fst, "different sample rates"
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]

    # 1. Alignment and polarity: cross-correlation peak, refined by a parabola.
    N = 1 << int(np.ceil(np.log2(2 * n)))
    xc = np.fft.irfft(np.fft.rfft(a, N) * np.conj(np.fft.rfft(b, N)), N)
    xc = np.concatenate([xc[-(n - 1):], xc[:n]])
    k = int(np.argmax(np.abs(xc)))
    lag = k - (n - 1)
    y0, y1, y2 = np.abs(xc[k - 1]), np.abs(xc[k]), np.abs(xc[k + 1])
    frac = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) != 0 else 0.0
    polarity = 1.0 if xc[k] > 0 else -1.0

    print("# Null test analysis\n")
    print("Reference: `%s`  \nTest: `%s`  \n%d Hz, %.1f s, channel %d\n" % (ref_path, test_path, fs, n / fs, ch))
    print("## 1. Alignment and polarity\n")
    print("- Offset: test is %s by %d samples (%+.3f fractional). %s" % (
        "later" if lag < 0 else "earlier", abs(lag), frac,
        "Aligned to the sample." if lag == 0 and abs(frac) < 0.05 else "**Not sample-aligned**: the null as rendered is limited by this."))
    print("- Polarity: %s" % ("same (the null needs one track inverted, as in a normal null test)" if polarity > 0 else "**inverted** relative to the reference"))

    # Shift the test by the integer lag for the remaining measurements.
    bs = np.roll(b, lag) * polarity
    s = slice(abs(lag) + int(0.1 * fs), n - abs(lag) - int(0.1 * fs))
    ra, rb = a[s], bs[s]

    # Transfer function test/reference (Welch cross-spectrum).
    nper = 1 << 14
    f, Saa = welch(ra, fs, nperseg=nper)
    _, Sbb = welch(rb, fs, nperseg=nper)
    _, Sab = csd(ra, rb, fs, nperseg=nper)     # E[conj(A) B]
    H = Sab / Saa                              # test relative to reference
    coh = np.abs(Sab) ** 2 / (Saa * Sbb + 1e-300)
    dm = 20 * np.log10(np.abs(H) + 1e-300)

    # 2. Level: the broadband offset, measured where two accurate EQs agree (median over 100 Hz - 2 kHz).
    band = (f >= 100) & (f <= 2000)
    off_db = float(np.median(dm[band]))
    g = 10 ** (-off_db / 20)
    print("\n## 2. Level\n")
    print("- Broadband level difference: test is %+.4f dB relative to the reference (median over 100 Hz-2 kHz)." % off_db)
    print("  A level error on its own limits a null to %.1f dB." % (20 * np.log10(abs(10 ** (off_db / 20) - 1)) if off_db != 0 else -300))

    # 3. Nulls.
    def null(x, y):
        d = x - y
        return (db(np.mean(d ** 2) / np.mean(x ** 2)), 20 * np.log10(np.max(np.abs(d)) / np.max(np.abs(x))),
                20 * np.log10(np.max(np.abs(d)) + 1e-300))
    print("\n## 3. Null depth (RMS of the difference relative to the reference; peak relative to its peak; peak in dBFS)\n")
    print("| | RMS | Peak | Peak dBFS |")
    print("|---|---|---|---|")
    print("| As rendered (no corrections) | %.1f dB | %.1f dB | %.1f |" % null(a[s], b[s]))
    print("| Offset and polarity corrected | %.1f dB | %.1f dB | %.1f |" % null(ra, rb))
    print("| ... and level offset removed | %.1f dB | %.1f dB | %.1f |" % null(ra, g * rb))

    # 4./5. Differences and residual by band, level offset removed.
    Hn = H * g
    dmn = 20 * np.log10(np.abs(Hn) + 1e-300)
    dp = np.degrees(np.angle(Hn))
    total = np.sum(Saa)
    print("\n## 4./5. Differences and residual by band (level offset removed)\n")
    print("Magnitude and phase are test relative to reference. The residual columns split the null by cause, in dB")
    print("relative to the whole signal; the larger one dominates that band. Coherence near 1 means the comparison is clean.\n")
    print("| Band | Largest magnitude difference | Largest phase difference | Residual: magnitude / phase | Coherence (min) |")
    print("|---|---|---|---|---|")
    for lo, hi in BANDS:
        m = (f >= lo) & (f < hi)
        if not m.any() or lo >= fs / 2:
            continue
        Sa = Saa[m]
        mag = (np.abs(Hn[m]) - 1) ** 2 * Sa
        ph = 2 * np.abs(Hn[m]) * (1 - np.cos(np.angle(Hn[m]))) * Sa
        print("| %g-%g Hz | %+.3f dB | %+.2f deg | %.0f / %.0f dB | %.4f |" % (
            lo, hi, dmn[m][np.argmax(np.abs(dmn[m]))], dp[m][np.argmax(np.abs(dp[m]))],
            db(np.sum(mag) / total), db(np.sum(ph) / total), np.min(coh[m])))


if __name__ == "__main__":
    args = [x for x in sys.argv[1:] if not x.startswith("--")]
    ch = int(sys.argv[sys.argv.index("--channel") + 1]) if "--channel" in sys.argv else 0
    main(args[0], args[1], ch)
