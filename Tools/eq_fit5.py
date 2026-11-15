# Five-point magnitude-squared interpolation: poles and zeros both fitted (closed form, 6x6 null space).
from eq_design import *

def cos_coeffs(p):   # |p0+p1 z^-1+p2 z^-2|^2 = C0 phi0 + C1 phi1 + C2 phi2
    return np.array([(p[0]+p[1]+p[2])**2, (p[0]-p[1]+p[2])**2, -4*p[0]*p[2]])

def factor(C):
    if C[0] < 0 or C[1] < 0: return None
    s0, s1 = np.sqrt(C[0]), np.sqrt(C[1])
    W = 0.5*(s0+s1); p1 = 0.5*(s0-s1)
    d = W*W + C[2]
    if d < 0: return None
    p0 = 0.5*(W+np.sqrt(d)); p2 = 0.5*(W-np.sqrt(d))
    return np.array([p0, p1, p2])

def fit5(kind, f0, g, Q, fs, pts):
    num, den = proto(kind, f0, g, Q)
    T = lambda wd: abs(H_analog(num, den, wd*fs))**2
    M = []
    for w in pts:
        p = np.array(phis(w)); t = T(w)
        M.append(np.r_[p, -t*p])          # B.phi - T A.phi = 0
    M = np.array(M)
    _, _, vt = np.linalg.svd(M)
    v = vt[-1]
    B, A = v[:3], v[3:]
    if A[0] < 0: B, A = -B, -A
    a = factor(A); b = factor(B)
    if a is None or b is None: return None
    return b/a[0], a/a[0]

def pts_for(kind, f0, Q, fs, variant):
    w0 = 2*np.pi*f0/fs
    if kind == 'bell':
        bw = 2/np.log(2)*np.arcsinh(1/(2*Q))   # octaves
        lo, hi = w0*2**(-bw/2), w0*2**(bw/2)
    else:
        lo, hi = w0/2, w0*2
    top = 0.92*np.pi
    hi = min(hi, 0.5*(w0+np.pi) if w0 < np.pi else top)
    if variant == 'A':
        return [0.0, lo, w0, hi, np.pi]
    if variant == 'B':
        return [0.0, lo, w0, min(hi, top), top]

def design_fit(variant):
    def d(kind, f0, g, Q, fs):
        if kind in ('hp', 'lp') or (kind == 'lowshelf'):
            return matched(kind, f0, g, Q, fs)
        r = fit5(kind, f0, g, Q, fs, pts_for(kind, f0, Q, fs, variant))
        if r is None:
            return matched(kind, f0, g, Q, fs)
        b, a = r
        if np.any(np.abs(np.roots(a)) >= 1) or np.any(np.abs(np.roots(b)) > 1):
            return matched(kind, f0, g, Q, fs)
        return b, a
    return d

if __name__ == '__main__':
    f = np.geomspace(20, 20000, 1500)
    fails = 0
    for kind, Qs, gains, f0s in (('bell', (0.3,0.5,0.7,1.0,1.5,2.5,4,7,10), (-15,-9,-6,-2,-0.5,0.5,2,6,9,15), (100,1000,2000,4000,6000,8000,10000,12000,14000,16000,18000,20000)),
                                 ('highshelf', (0.4,0.5,0.71,1.0,1.4), (-15,-6,-1,1,6,15), (1000,2000,4000,8000,12000,16000,20000))):
        for f0 in f0s:
            line = '%-9s %6d |' % (kind, f0)
            for fs in (44100, 48000, 96000):
                res = []
                for des in (matched, design_fit('A'), design_fit('B')):
                    e = 0
                    for g in gains:
                        for Q in Qs:
                            num, den = proto(kind, f0, g, Q)
                            ref = 20*np.log10(np.abs(H_analog(num, den, 2*np.pi*f)))
                            b, a = des(kind, f0, g, Q, fs)
                            m = 20*np.log10(np.abs(H_digital(b, a, 2*np.pi*f/fs)))
                            e = max(e, np.max(np.abs(m-ref)))
                    res.append(e)
                line += ' %5.2f %5.2f %5.2f |' % tuple(res)
            print(line)
    # how often does the fit fall back?
    for variant in 'AB':
        n = nf = 0
        for kind in ('bell','highshelf'):
            for fs in (44100, 48000, 96000, 192000):
                for f0 in np.geomspace(20, 20000, 40):
                    for Q in np.geomspace(0.3, 10, 12):
                        for g in np.linspace(-15, 15, 13):
                            if g == 0: continue
                            n += 1
                            r = fit5(kind, f0, g, Q, fs, pts_for(kind, f0, Q, fs, variant))
                            if r is None or np.any(np.abs(np.roots(r[1])) >= 1) or np.any(np.abs(np.roots(r[0])) > 1): nf += 1
        print('variant', variant, 'fallbacks', nf, 'of', n)
