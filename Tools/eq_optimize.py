# Offline experiment: minimax-like (L8 then L30 of dB error) optimal biquads for bells and high shelves,
# on a grid of (f0/fs, Q, gain), to see whether a tabulated design can beat the matched design.
import numpy as np, json, sys, time
from scipy.optimize import minimize
from eq_design import proto, H_analog, H_digital, matched

W = np.geomspace(2*np.pi*10/44100, 0.907*np.pi, 500)   # digital band: ~10 Hz .. 20 kHz at 44.1k
def opt(kind, x, Q, g):
    fs = 1.0
    num, den = proto(kind, x, g, Q)
    ref = 20*np.log10(np.abs(H_analog(num, den, W)))
    b, a = matched(kind, x, g, Q, fs)
    x0 = np.r_[b, a[1:]]
    def err(v):
        m = 20*np.log10(np.abs(H_digital(v[:3], np.r_[1, v[3:]], W)) + 1e-30)
        return m - ref
    e0 = np.max(np.abs(err(x0)))
    best = x0
    for p in (8, 30):
        r = minimize(lambda v: np.mean(np.abs(err(v))**p)**(1/p), best, method='Nelder-Mead',
                     options=dict(maxiter=6000, xatol=1e-13, fatol=1e-14))
        best = r.x
    e1 = np.max(np.abs(err(best)))
    ok = np.all(np.abs(np.roots(np.r_[1, best[3:]])) < 1) and np.all(np.abs(np.roots(best[:3])) < 1)
    return e0, e1, best.tolist(), bool(ok)

xs = np.geomspace(0.02, 0.47, 20)
out = []
t = time.time()
for kind, Qs in (('bell', np.geomspace(0.3, 10, 12)), ('highshelf', np.geomspace(0.4, 1.6, 6))):
    for x in xs:
        for Q in Qs:
            for g in (0.5, 1, 2, 3, 4.5, 6, 9, 12, 15, 18):
                e0, e1, c, ok = opt(kind, x, Q, g)
                out.append(dict(kind=kind, x=x, Q=Q, g=g, matched=e0, optimal=e1, c=c, ok=ok))
        print(kind, '%.3f' % x, 'done', '%.0fs' % (time.time()-t), flush=True)
        json.dump(out, open('eq_optimal_grid.json', 'w'))
print('finished', flush=True)
