# Builds Source/dsp/EqFitTable.h from the offline optimisation results and validates the interpolated
# design at random off-grid settings (the plug-in interpolates exactly the same way).
import numpy as np, json, sys, os
from eq_design import proto, H_analog, H_digital, matched, svf_from_biquad
from eq_optimize2 import biquad_from_svf, svf_vec  # noqa (module guarded below)

XMIN, XMAX, NX = 0.005, 0.47, 30
BOOST = [0.25, 0.5, 1, 2, 3, 4.5, 6, 9, 12, 15, 18]
GAINS = [-g for g in reversed(BOOST)] + [0.0] + BOOST
W = np.geomspace(2*np.pi*10/44100, 0.907*np.pi, 500)

def invert(b, a):
    return np.array(a)/b[0], np.array(b)/b[0]

def build(kind, path):
    d = []
    for pth in (path if isinstance(path, (list, tuple)) else [path]):
        d += json.load(open(pth))
    Qs = sorted(set(round(r['Q'], 9) for r in d))
    xs = sorted(set(r['x'] for r in d))
    assert len(xs) == NX
    T = np.zeros((len(Qs), len(GAINS), NX, 5))
    idx = {(round(r['Q'], 9), r['g'], round(r['x'], 12)): r for r in d}
    bad = 0
    for qi, Q in enumerate(Qs):
        for xi, x in enumerate(xs):
            for gi, g in enumerate(GAINS):
                vm = svf_vec(*matched(kind, x, g, Q, 1.0))
                if g == 0.0:
                    v = vm.copy()
                else:
                    r = idx[(round(Q, 9), abs(g), round(x, 12))]
                    if not r['ok']: bad += 1
                    v = np.array(r['v'])
                    if g < 0:
                        b, a = biquad_from_svf(v)
                        v = svf_vec(*invert(b, a))
                T[qi, gi, xi] = v - vm
    # At the top grid column (f0 = 0.47 fs, above the optimisation band) narrow bells have no well-defined
    # optimum; fall back to the matched design there so the last cell interpolates towards it smoothly.
    for qi, Q in enumerate(Qs):
        if Q > 2.0:
            T[qi, :, NX-1] = 0.0
    print(kind, 'grid', T.shape, 'non-minphase points', bad)
    return np.array(Qs), np.array(xs), T

def lookup(T, Qs, x, Q, g):
    ux = np.log(x/XMIN)/np.log(XMAX/XMIN)*(NX-1)
    uq = np.log(Q/Qs[0])/np.log(Qs[-1]/Qs[0])*(len(Qs)-1)
    fx = np.clip(ux, 0, NX-1.000001); fq = np.clip(uq, 0, len(Qs)-1.000001)
    ix, iq = int(fx), int(fq); tx, tq = fx-ix, fq-iq
    g = np.clip(g, GAINS[0], GAINS[-1]); ig = 0
    while ig < len(GAINS)-2 and g > GAINS[ig+1]: ig += 1
    tg = (g-GAINS[ig])/(GAINS[ig+1]-GAINS[ig])
    v = np.zeros(5)
    for c in range(8):
        dq, dg, dx = c & 1, (c >> 1) & 1, (c >> 2) & 1
        w = (tq if dq else 1-tq)*(tg if dg else 1-tg)*(tx if dx else 1-tx)
        v += w*T[iq+dq, ig+dg, ix+dx]
    return v

def validate(kind, Qs, T, n=3000, seed=1, qrange=None):
    rng = np.random.default_rng(seed)
    errs_fit, errs_m, f0errs = [], [], []
    worst = (0, None)
    qlo, qhi = qrange or (Qs[0], Qs[-1])
    for _ in range(n):
        x = np.exp(rng.uniform(np.log(XMIN), np.log(XMAX)))
        Q = np.exp(rng.uniform(np.log(qlo), np.log(qhi)))
        g = rng.uniform(-15, 15)
        num, den = proto(kind, x, g, Q)
        ref = 20*np.log10(np.abs(H_analog(num, den, W)))
        v = lookup(T, Qs, x, Q, g) + svf_vec(*matched(kind, x, g, Q, 1.0))
        assert np.exp(v[0]) > 0 and np.exp(v[1]) > 0
        b, a = biquad_from_svf(v)
        assert np.all(np.abs(np.roots(a)) < 1)
        e = np.max(np.abs(20*np.log10(np.abs(H_digital(b, a, W))) - ref))
        bm, am = matched(kind, x, g, Q, 1.0)
        em = np.max(np.abs(20*np.log10(np.abs(H_digital(bm, am, W))) - ref))
        e0 = abs(20*np.log10(abs(H_digital(b, a, np.array([2*np.pi*x])))[0]) - 20*np.log10(abs(H_analog(num, den, np.array([2*np.pi*x])))[0]))
        f0errs.append(e0)
        errs_fit.append(e); errs_m.append(em)
        if e > worst[0]: worst = (e, (x, Q, g, em))
    errs_fit, errs_m = np.array(errs_fit), np.array(errs_m)
    print('%s random settings: fitted  max %.3f dB  p99 %.3f  median %.4f | matched max %.3f  p99 %.3f  median %.4f'
          % (kind, errs_fit.max(), np.percentile(errs_fit, 99), np.median(errs_fit), errs_m.max(), np.percentile(errs_m, 99), np.median(errs_m)))
    print('   worst fitted at x=%.4f Q=%.3f g=%.2f (matched there %.3f); worst gain error at f0 %.4f dB' % (worst[1][0], worst[1][1], worst[1][2], worst[1][3], max(f0errs)))
    return errs_fit, errs_m

def cell_error(kind, Qs, T, xs, qi, xi, gi, pts):
    worst = wm = 0.0
    for (a, b, c) in pts:
        x = xs[xi]*(xs[xi+1]/xs[xi])**a; Q = Qs[qi]*(Qs[qi+1]/Qs[qi])**b; g = GAINS[gi] + c*(GAINS[gi+1]-GAINS[gi])
        num, den = proto(kind, x, g, Q); ref = 20*np.log10(np.abs(H_analog(num, den, W)))
        v = lookup(T, Qs, x, Q, g) + svf_vec(*matched(kind, x, g, Q, 1.0))
        b_, a_ = biquad_from_svf(v)
        e = np.max(np.abs(20*np.log10(np.abs(H_digital(b_, a_, W))) - ref))
        bm, am = matched(kind, x, g, Q, 1.0)
        em = np.max(np.abs(20*np.log10(np.abs(H_digital(bm, am, W))) - ref))
        worst = max(worst, e); wm = max(wm, em)
    return worst, wm

def clean(kind, Qs, xs, T):
    # Where neighbouring optimum solutions belong to different families, interpolating between them is worse
    # than the matched design. Zero the correction at the corners of every such cell (falls back to matched)
    # and repeat until every cell is at least as good as the matched design.
    pts = [(0.5, 0.5, 0.5), (0.25, 0.75, 0.3), (0.75, 0.25, 0.7), (0.5, 0.5, 0.15), (0.5, 0.5, 0.85)]
    zeroed = set()
    cells = [(qi, xi, gi) for qi in range(len(Qs)-1) for xi in range(NX-1) for gi in range(len(GAINS)-1)]
    for it in range(6):
        bad = []
        for (qi, xi, gi) in cells:
            worst, wm = cell_error(kind, Qs, T, xs, qi, xi, gi, pts)
            if worst > max(0.2, 1.1*wm):
                bad.append((qi, xi, gi))
        print('  %s pass %d: %d cells worse than matched' % (kind, it, len(bad)))
        if not bad:
            break
        for (qi, xi, gi) in bad:
            for dq in (0, 1):
                for dx in (0, 1):
                    for dg in (0, 1):
                        T[qi+dq, gi+dg, xi+dx] = 0.0
                        zeroed.add((qi+dq, gi+dg, xi+dx))
        cells = sorted(set((max(0, min(len(Qs)-2, q+a)), max(0, min(NX-2, x+b)), max(0, min(len(GAINS)-2, g+c)))
                           for (q, x, g) in bad for a in (-1, 0, 1) for b in (-1, 0, 1) for c in (-1, 0, 1)))
    print('  %s: %d of %d grid points fall back to matched' % (kind, len(zeroed), T.shape[0]*T.shape[1]*T.shape[2]))
    return T

def emit(tables, path):
    out = ['// Generated by Tools/make_fit_table.py from Tools/eq_optimize3.py (v0.2). Do not edit.',
           '// Corrections to the matched design in SVF parameters (d log g, d log k, d m0, d m1, d m2)', '// on a grid of (Q, gain, f0/fs): fitted = matched + interpolated correction.',
           '#pragma once', '', 'namespace coherence::dsp::fit', '{',
           'struct Table { int numQ; double qMin, qMax; const float* data; };',
           'constexpr bool available = true;',
           '// Fitted for 44.1/48 kHz (audio band 10 Hz..20 kHz at 44.1 kHz). At 88.2 kHz and above the matched design is used.',
           'constexpr double maxSampleRate = 80000.0;',
           'constexpr int numX = %d;' % NX,
           'constexpr double xMin = %r, xMax = %r;' % (XMIN, XMAX),
           'constexpr int numGains = %d;' % len(GAINS),
           'constexpr double gains[numGains] = { %s };' % ', '.join('%r' % float(g) for g in GAINS)]
    for name, (Qs, T) in tables.items():
        flat = T.reshape(-1)
        out.append('inline const float %sData[%d] = {' % (name, flat.size))
        for i in range(0, flat.size, 10):
            out.append('    ' + ', '.join('%.9ef' % v for v in flat[i:i+10]) + ',')
        out.append('};')
        out.append('inline const Table %s { %d, %r, %r, %sData };' % (name, len(Qs), float(Qs[0]), float(Qs[-1]), name))
    out.append('} // namespace coherence::dsp::fit')
    open(path, 'w').write('\n'.join(out) + '\n')
    print('wrote', path, os.path.getsize(path)//1024, 'KB')

def emit_v3(tables, path):
    # tables: {name: (Qs, T, xmin, xmax)}
    out = ['// Generated by Tools/make_fit_table.py from Tools/eq_optimize3.py (v0.2). Do not edit.',
           '// Corrections to the matched design in SVF parameters (d log g, d log k, d m0, d m1, d m2)',
           '// on a grid of (Q, gain, f0/fs): fitted = matched + interpolated correction.',
           '// Family A is fitted for 44.1/48 kHz (10 Hz..20 kHz at 44.1 kHz), family B for 88.2 kHz and up',
           '// (10 Hz..24 kHz at 88.2 kHz).',
           '#pragma once', '', 'namespace coherence::dsp::fit', '{',
           'struct Table { int numQ; double qMin, qMax; int numX; double xMin, xMax; const float* data; };',
           'constexpr bool available = true;',
           'constexpr double familySplitRate = 80000.0;',
           'constexpr int numGains = %d;' % len(GAINS),
           'constexpr double gains[numGains] = { %s };' % ', '.join('%r' % float(g) for g in GAINS)]
    for name, (Qs, T, xmin, xmax) in tables.items():
        flat = T.reshape(-1)
        out.append('inline const float %sData[%d] = {' % (name, flat.size))
        for i in range(0, flat.size, 10):
            out.append('    ' + ', '.join('%.9ef' % v for v in flat[i:i+10]) + ',')
        out.append('};')
        out.append('inline const Table %s { %d, %r, %r, %d, %r, %r, %sData };' % (name, len(Qs), float(Qs[0]), float(Qs[-1]), T.shape[2], float(xmin), float(xmax), name))
    for n in ('bellA', 'highShelfA', 'bellB', 'highShelfB'):
        if n not in tables:
            out.append('inline const Table* const %sPtr = nullptr;' % n)
        else:
            out.append('inline const Table* const %sPtr = &%s;' % (n, n))
    out.append('} // namespace coherence::dsp::fit')
    open(path, 'w').write('\n'.join(out) + '\n')
    print('wrote', path, os.path.getsize(path)//1024, 'KB')

if __name__ == '__main__' and '--v3' in sys.argv:
    import glob
    tables = {}
    families = (('A', (0.005, 0.47)), ('B', (0.0025, 0.24))) if '--with-B' in sys.argv else (('A', (0.005, 0.47)),)
    for fam, (xmin, xmax) in families:
        XMIN, XMAX = xmin, xmax
        globals()['XMIN'], globals()['XMAX'] = xmin, xmax
        globals()['W'] = np.geomspace(2*np.pi*10/44100, 0.907*np.pi, 500) if fam == 'A' else np.geomspace(2*np.pi*10/88200, 2*np.pi*24000/88200, 500)
        for kind, name in (('bell', 'bell'), ('highshelf', 'highShelf')):
            files = sorted(glob.glob('fit3_%s_%s_p*.json' % (kind, fam)))
            if len(files) < 2:
                print('skipping', kind, fam, '(not computed)')
                continue
            Q, x, T = build(kind, files)
            T = clean(kind, Q, x, T)
            if fam == 'A':
                validate(kind, Q, T, qrange=(0.12, 10) if kind == 'bell' else None)
            tables[name + fam] = (Q, T, xmin, xmax)
    if '--emit' in sys.argv:
        emit_v3(tables, '../Source/dsp/EqFitTable.h')
    sys.exit(0)

if False:
    import glob
    tables = {}
    Qb, xb, Tb = build('bell', sorted(glob.glob('fit3_bell_A_p*.json')))
    Tb = clean('bell', Qb, xb, Tb)
    validate('bell', Qb, Tb, qrange=(0.12, 10))
    tables['bell'] = (Qb, Tb)
    Qh, xh, Th = build('highshelf', sorted(glob.glob('fit3_highshelf_A_p*.json')))
    Th = clean('highshelf', Qh, xh, Th)
    validate('highshelf', Qh, Th)
    tables['highShelf'] = (Qh, Th)
    if '--emit' in sys.argv:
        emit(tables, '../Source/dsp/EqFitTable.h')
elif __name__ == '__main__':
    tables = {}
    Qb, xb, Tb = build('bell', 'fit_bell.json')
    Tb = clean('bell', Qb, xb, Tb)
    validate('bell', Qb, Tb, qrange=(0.3, 10))
    tables['bell'] = (Qb, Tb)
    if os.path.exists('fit_highshelf.json'):
        Qh, xh, Th = build('highshelf', 'fit_highshelf.json')
        Th = clean('highshelf', Qh, xh, Th)
        validate('highshelf', Qh, Th)
        tables['highShelf'] = (Qh, Th)
    if '--emit' in sys.argv:
        if 'highShelf' not in tables:
            sys.exit('high shelf results missing')
        emit(tables, '../Source/dsp/EqFitTable.h')
