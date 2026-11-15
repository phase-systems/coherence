# Offline design of the COHERENCE "fitted" EQ coefficients (bells and high shelves, boosts).
# For each grid point the biquad is optimised to minimise the dB error against the analog prototype over
# ~10 Hz .. 0.907*Nyquist (20 kHz at 44.1 kHz), first in an L8 sense, then L30 (close to minimax).
# Solutions are continued along f0 so neighbouring grid points belong to the same smooth family, which is
# what makes them safe to interpolate. The result is stored in SVF form (log g, log k, m0, m1, m2).
import numpy as np, json, time, sys
from scipy.optimize import minimize
from eq_design import proto, H_analog, H_digital, matched, svf_from_biquad

W = np.geomspace(2*np.pi*10/44100, 0.907*np.pi, 600)

def svf_vec(b, a):
    g, k, m0, m1, m2 = svf_from_biquad(b, a)
    return np.array([np.log(g), np.log(k), m0, m1, m2])

def biquad_from_svf(v):
    g, k, m0, m1, m2 = np.exp(v[0]), np.exp(v[1]), v[2], v[3], v[4]
    # transfer: m0 + m1*BP + m2*LP with trapezoidal SVF
    D = np.array([1 + g*k + g*g, 2*g*g - 2, 1 - g*k + g*g])
    hp, bp, lp = np.array([1., -2., 1.]), np.array([g, 0., -g]), np.array([g*g, 2*g*g, g*g])
    N = m0*D + m1*bp + m2*lp
    return N/D[0], D/D[0]

def run(kind, Qs, gains, xs):
    out = []
    for Q in Qs:
        for g in gains:
            prev = None
            for x in xs:
                num, den = proto(kind, x, g, Q)
                ref = 20*np.log10(np.abs(H_analog(num, den, W)))
                bm, am = matched(kind, x, g, Q, 1.0)
                vm = svf_vec(bm, am)
                v0 = vm if prev is None else vm + prev
                def err(v):
                    b, a = biquad_from_svf(v)
                    return 20*np.log10(np.abs(H_digital(b, a, W)) + 1e-30) - ref
                def cost(v, p):
                    b, a = biquad_from_svf(v)
                    pen = 0.0
                    rz = np.max(np.abs(np.roots(b)))
                    if rz > 0.9995: pen = 1e3*(rz-0.9995)
                    return np.mean(np.abs(err(v))**p)**(1/p) + pen
                best = v0
                for p in (8, 30):
                    r = minimize(lambda v: cost(v, p), best, method='Nelder-Mead',
                                 options=dict(maxiter=8000, xatol=1e-12, fatol=1e-14, adaptive=True))
                    best = r.x
                e_m = np.max(np.abs(err(vm))); e_o = np.max(np.abs(err(best)))
                if e_o > e_m:      # never worse than matched
                    best, e_o = vm, e_m
                prev = best - vm
                b, a = biquad_from_svf(best)
                ok = bool(np.all(np.abs(np.roots(a)) < 1) and np.all(np.abs(np.roots(b)) < 1))
                out.append(dict(kind=kind, x=x, Q=Q, g=g, matched=e_m, optimal=e_o, v=best.tolist(), vm=vm.tolist(), ok=ok))
        print(kind, 'Q=%.3f' % Q, 'done %.0fs' % (time.time()-t0), flush=True)
    return out

if __name__ == '__main__':
    t0 = time.time()
    xs = np.geomspace(0.005, 0.47, 30)
    gains = [0.25, 0.5, 1, 2, 3, 4.5, 6, 9, 12, 15, 18]
    res = run('bell', np.geomspace(0.25, 12, 13), gains, xs)
    json.dump(res, open('fit_bell.json', 'w'))
    res2 = run('highshelf', np.geomspace(0.35, 2.0, 7), gains, xs)
    json.dump(res2, open('fit_highshelf.json', 'w'))
    print('finished', flush=True)
