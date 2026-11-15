#!/usr/bin/env python3
"""Each render against the analog bell it targets (classic / MDW Q definition), 20 Hz-16 kHz and 16-20 kHz."""
import sys
from pathlib import Path
import numpy as np
from q_analyze import load_ir, magnitude_db, bell_db, NAME

def analog(f, f0, q, g):
    G = 10 ** (abs(g) / 20)
    d = bell_db(f, f0, q, q / G)      # boost: poles Q, zeros Q/G
    return d if g >= 0 else -d        # cut: exact reciprocal

print("| Render | Max deviation from analog, 20 Hz-16 kHz | 16-20 kHz |")
print("|---|---|---|")
for folder in sys.argv[1:]:
    for p in sorted(Path(folder).glob("*.wav")):
        m = NAME.match(p.stem)
        if not m:
            continue
        f0 = float(m["f"]) * (1000 if m["k"] else 1)
        fs, ir = load_ir(p)
        f = np.geomspace(20, 20000, 3000)
        d = magnitude_db(ir, fs, f) - analog(f, f0, float(m["q"]), float(m["g"]))
        lo = f <= 16000
        print("| %s | %.3f dB | %.3f dB |" % (p.stem, np.max(np.abs(d[lo])), np.max(np.abs(d[~lo]))))
