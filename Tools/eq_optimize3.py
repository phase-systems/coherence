#!/usr/bin/env python3
"""COHERENCE v0.2 fitted EQ design (offline).

Changes from eq_optimize2.py, after the v0.2 review:
  * The gain at f0 is held exact (strong penalty): the set gain is always exactly the gain at the set
    frequency. v0.1 spread its residual error over the whole band, which moved the centre gain by up to
    0.08 dB on wide bells.
  * The fit band is the audio band, per sample-rate family, instead of 0.907 x Nyquist for every rate:
      family A (44.1 / 48 kHz):  10 Hz .. 20 kHz at 44.1 kHz  (normalised 0.000227 .. 0.4535)
      family B (88.2 kHz and up): 10 Hz .. 24 kHz at 88.2 kHz (normalised 0.000113 .. 0.272)
    v0.1 used one table fitted up to 43.5 kHz at 96 kHz, which traded audible accuracy for ultrasonic.
  * The Q grid reaches 0.1, because v0.2 defines Q like MDWEQ6 (band-pass Q of the boost); the RBJ-style
    Q the design uses is Q / 10^(|gain|/40), down to 0.3 / 2.37 = 0.127.

Usage: python3 eq_optimize3.py <bell|highshelf> <A|B> [part 0|1] [resume]
  part   fits every other Q value (0: even, 1: odd), so two processes can share the work.
  resume keeps the Q values already complete in the output file and fits only the rest.
"""
import json
import os
import sys
import time

import numpy as np
from scipy.optimize import minimize

from eq_design import H_analog, H_digital, matched, proto
from eq_optimize2 import biquad_from_svf, svf_vec

FAMILY = {
    "A": dict(band=(10.0 / 44100.0, 20000.0 / 44100.0), xs=np.geomspace(0.005, 0.47, 30)),
    "B": dict(band=(10.0 / 88200.0, 24000.0 / 88200.0), xs=np.geomspace(0.0025, 0.24, 30)),
}
GAINS = [0.25, 0.5, 1, 2, 3, 4.5, 6, 9, 12, 15, 18]
QS = {"bell": np.geomspace(0.1, 12.0, 14), "highshelf": np.geomspace(0.35, 2.0, 7)}


def run(kind, fam, part=None, resume=False):
    lo, hi = FAMILY[fam]["band"]
    W = np.geomspace(2 * np.pi * lo, 2 * np.pi * hi, 280)
    out = []
    t0 = time.time()
    qs = QS[kind] if part is None else QS[kind][part::2]
    path = "fit3_%s_%s%s.json" % (kind, fam, "" if part is None else "_p%d" % part)
    done = set()
    if resume:
        # Keep every Q value that is complete in the saved file (all gains x all x). Each Q value is fitted
        # independently (the warm start resets per Q and gain), so a resumed run equals an uninterrupted one.
        try:
            saved = json.load(open(path))
        except FileNotFoundError:
            saved = []
        need = len(GAINS) * len(FAMILY[fam]["xs"])
        for Q in qs:
            rows = [e for e in saved if abs(e["Q"] - Q) < 1e-9 * Q]
            if len(rows) == need:
                out += rows
                done.add(float(Q))
        print(kind, fam, "resuming: %d of %d Q values already done" % (len(done), len(qs)), flush=True)
    for Q in qs:
        if float(Q) in done:
            continue
        for g in GAINS:
            prev = None
            for x in FAMILY[fam]["xs"]:
                num, den = proto(kind, x, g, Q)
                ref = 20 * np.log10(np.abs(H_analog(num, den, W)))
                w0 = np.array([2 * np.pi * x])
                ref0 = 20 * np.log10(np.abs(H_analog(num, den, w0)))
                bm, am = matched(kind, x, g, Q, 1.0)
                vm = svf_vec(bm, am)
                v0 = vm if prev is None else vm + prev

                def err(v, w=W, r=ref):
                    b, a = biquad_from_svf(v)
                    return 20 * np.log10(np.abs(H_digital(b, a, w)) + 1e-30) - r

                def cost(v, p):
                    b, a = biquad_from_svf(v)
                    pen = 0.0
                    disc = b[1] * b[1] - 4 * b[0] * b[2]
                    if disc < 0:
                        rz = np.sqrt(abs(b[2] / b[0]))
                    else:
                        sq = np.sqrt(disc)
                        rz = max(abs((-b[1] + sq) / (2 * b[0])), abs((-b[1] - sq) / (2 * b[0])))
                    if rz > 0.9995:
                        pen += 1e3 * (rz - 0.9995)
                    return np.mean(np.abs(err(v)) ** p) ** (1 / p) + pen + 50.0 * abs(err(v, w0, ref0)[0])

                best = v0
                for p in (8, 30):
                    r = minimize(lambda v: cost(v, p), best, method="Nelder-Mead",
                                 options=dict(maxiter=2500, xatol=1e-11, fatol=1e-12, adaptive=True))
                    best = r.x
                e_m = np.max(np.abs(err(vm)))
                e_o = np.max(np.abs(err(best)))
                e_f0 = abs(err(best, w0, ref0)[0])
                if e_o > e_m or e_f0 > 0.002:   # never worse than matched, centre gain within 0.002 dB
                    best, e_o, e_f0 = vm, e_m, abs(err(vm, w0, ref0)[0])
                prev = best - vm
                b, a = biquad_from_svf(best)
                ok = bool(np.all(np.abs(np.roots(a)) < 1) and np.all(np.abs(np.roots(b)) < 1))
                out.append(dict(kind=kind, x=float(x), Q=float(Q), g=g, matched=float(e_m), optimal=float(e_o),
                                f0err=float(e_f0), v=best.tolist(), ok=ok))
        print(kind, fam, "Q=%.3f done %.0fs" % (Q, time.time() - t0), flush=True)
        tmp = path + ".tmp"
        json.dump(out, open(tmp, "w"))
        os.replace(tmp, path)   # atomic: an interrupted save never leaves a truncated file
    print("finished", flush=True)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "resume"]
    run(args[0], args[1], int(args[2]) if len(args) > 2 else None, resume="resume" in sys.argv[1:])
