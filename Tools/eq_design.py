# Prototype of the PHASE SYSTEMS EQ design (mirrors Source/dsp/EqDesign.h).
# Matched 2nd-order design (Vicanek 2016): poles by impulse invariance, numerator fitted so |H| matches the
# analog prototype at DC, Nyquist and the band frequency. Boosts are built as the exact inverse of the cut.
import numpy as np

def analog(kind, f0, gdb, Q):
    """Analog prototype as (n2,n1,n0),(d2,d1,d0) in s, with w in rad/s (frequency f0 Hz)."""
    w = 2*np.pi*f0
    A = 10**(gdb/40.0)
    if kind == 'bell':
        return (1, A*w/Q, w*w), (1, w/(A*Q), w*w)
    if kind == 'lowshelf':
        sA = np.sqrt(A)
        return (A, sA*w/Q*A, A*A*w*w), (A, sA*w/Q, w*w)   # A*(A s^2 + ..)/(...) form below
    raise ValueError

def H_analog(num, den, w):
    s = 1j*w
    return (num[0]*s*s+num[1]*s+num[2])/(den[0]*s*s+den[1]*s+den[2])

def proto(kind, f0, gdb, Q):
    w = 2*np.pi*f0
    A = 10**(gdb/40.0)
    sA = np.sqrt(A)
    if kind == 'bell':
        return (1, A*w/Q, w*w), (1, w/(A*Q), w*w)
    if kind == 'lowshelf':   # RBJ: A*(s^2 + sqrt(A)/Q w s + A w^2)/(A s^2 + sqrt(A)/Q w s + w^2)
        return (A, A*sA*w/Q, A*A*w*w), (A, sA*w/Q, w*w)
    if kind == 'highshelf':  # RBJ: A*(A s^2 + sqrt(A)/Q w s + w^2)/(s^2 + sqrt(A)/Q w s + A w^2)
        return (A*A, A*sA*w/Q, A*w*w), (1, sA*w/Q, A*w*w)
    if kind == 'hp':
        return (1, 0, 0), (1, w/Q, w*w)
    if kind == 'lp':
        return (0, 0, w*w), (1, w/Q, w*w)

def poles(den, fs):
    d2, d1, d0 = den
    wp = np.sqrt(d0/d2)/fs            # rad/sample
    zeta = d1/(2*np.sqrt(d0*d2))
    if zeta <= 1:
        a1 = -2*np.exp(-zeta*wp)*np.cos(np.sqrt(1-zeta*zeta)*wp)
    else:
        a1 = -2*np.exp(-zeta*wp)*np.cosh(np.sqrt(zeta*zeta-1)*wp)
    a2 = np.exp(-2*zeta*wp)
    return a1, a2

def phis(w):
    p1 = np.sin(w/2)**2
    p0 = 1-p1
    return p0, p1, 4*p0*p1

def matched(kind, f0, gdb, Q, fs):
    # Bells are designed as boosts (poles stay at f0), shelves as cuts (poles stay at or below f0);
    # the opposite sign is the exact inverse, so boost and cut are perfect reciprocals.
    if (kind in ('bell','lowshelf') and gdb < 0) or (kind == 'highshelf' and gdb > 0):
        b, a = matched(kind, f0, -gdb, Q, fs)
        return np.array(a)/b[0], np.array(b)/b[0]
    num, den = proto(kind, f0, gdb, Q)
    a1, a2 = poles(den, fs)
    A0, A1, A2 = (1+a1+a2)**2, (1-a1+a2)**2, -4*a2
    wm = 2*np.pi*f0/fs
    p0, p1, p2 = phis(wm)
    mag2 = lambda wd: abs(H_analog(num, den, wd*fs))**2
    if kind == 'hp':
        b0 = np.sqrt(A0*p0+A1*p1+A2*p2)*Q/(4*p1)
        return np.array([b0,-2*b0,b0]), np.array([1,a1,a2])
    if kind == 'lp':
        R1 = (A0*p0+A1*p1+A2*p2)*Q*Q
        B0 = A0; B1 = (R1-B0*p0)/p1
        b0 = 0.5*(np.sqrt(B0)+np.sqrt(B1)); b1 = np.sqrt(B0)-b0
        return np.array([b0,b1,0.0]), np.array([1,a1,a2])
    B0 = mag2(0.0)*A0
    B1 = mag2(np.pi)*A1
    B2 = (mag2(wm)*(A0*p0+A1*p1+A2*p2) - B0*p0 - B1*p1)/p2
    W = 0.5*(np.sqrt(B0)+np.sqrt(B1))
    b1 = 0.5*(np.sqrt(B0)-np.sqrt(B1))
    disc = W*W+B2
    if disc < 0: print('INFEASIBLE', kind, f0, gdb, Q, fs, disc); disc = 0.0
    b0 = 0.5*(W+np.sqrt(disc)); b2 = 0.5*(W-np.sqrt(disc))
    return np.array([b0,b1,b2]), np.array([1,a1,a2])

def rbj_blt(kind, f0, gdb, Q, fs):
    """Standard bilinear (RBJ cookbook) for comparison."""
    A = 10**(gdb/40); w0 = 2*np.pi*f0/fs; al = np.sin(w0)/(2*Q); c = np.cos(w0)
    if kind == 'bell':
        b = [1+al*A, -2*c, 1-al*A]; a = [1+al/A, -2*c, 1-al/A]
    elif kind == 'highshelf':
        sA = 2*np.sqrt(A)*al
        b = [A*((A+1)+(A-1)*c+sA), -2*A*((A-1)+(A+1)*c), A*((A+1)+(A-1)*c-sA)]
        a = [(A+1)-(A-1)*c+sA, 2*((A-1)-(A+1)*c), (A+1)-(A-1)*c-sA]
    elif kind == 'lowshelf':
        sA = 2*np.sqrt(A)*al
        b = [A*((A+1)-(A-1)*c+sA), 2*A*((A-1)-(A+1)*c), A*((A+1)-(A-1)*c-sA)]
        a = [(A+1)+(A-1)*c+sA, -2*((A-1)+(A+1)*c), (A+1)+(A-1)*c-sA]
    elif kind == 'hp':
        b = [(1+c)/2, -(1+c), (1+c)/2]; a = [1+al, -2*c, 1-al]
    elif kind == 'lp':
        b = [(1-c)/2, 1-c, (1-c)/2]; a = [1+al, -2*c, 1-al]
    b = np.array(b)/a[0]; a = np.array(a)/a[0]
    return b, a

def H_digital(b, a, w):
    z = np.exp(-1j*w)
    return (b[0]+b[1]*z+b[2]*z*z)/(a[0]+a[1]*z+a[2]*z*z)

def svf_from_biquad(b, a):
    b0,b1,b2 = b; _,a1,a2 = a
    g = np.sqrt((1+a1+a2)/(1-a1+a2))
    k = 2*(1-a2)/(g*(1-a1+a2))
    c = 4/(1-a1+a2)
    hp = c*(b0-b1+b2)/4; bp = c*(b0-b2)/2; lp = c*(b0+b1+b2)/4
    return g, k, hp, bp/g - k*hp, lp/(g*g) - hp

def svf_run(x, g, k, m0, m1, m2):
    a1 = 1/(1+g*(g+k)); a2 = g*a1; a3 = g*a2
    ic1 = ic2 = 0.0
    y = np.empty_like(x)
    for n, v0 in enumerate(x):
        v3 = v0-ic2
        v1 = a1*ic1+a2*v3
        v2 = ic2+a2*ic1+a3*v3
        ic1 = 2*v1-ic1; ic2 = 2*v2-ic2
        y[n] = m0*v0+m1*v1+m2*v2
    return y

if __name__ == '__main__':
    f = np.geomspace(10, 20000, 2000)
    worst = {}
    for fs in (44100, 48000, 96000):
        wd = 2*np.pi*f/fs
        for kind in ('bell','lowshelf','highshelf','hp','lp'):
            for f0 in (20, 60, 200, 1000, 4000, 10000, 16000, 20000):
                if kind == 'lowshelf' and f0 > 2000: continue
                if kind in ('highshelf','lp') and f0 < 500: continue
                for gdb in ((-15,-6,-1,1,6,15) if kind not in ('hp','lp') else (0,)):
                    for Q in (0.3, 0.71, 1.5, 4, 10) if kind=='bell' else (0.5, 0.71, 1.0, 2.0):
                        num, den = proto(kind, f0, gdb, Q)
                        ref = 20*np.log10(np.abs(H_analog(num, den, 2*np.pi*f)))
                        b, a = matched(kind, f0, gdb, Q, fs)
                        m = 20*np.log10(np.abs(H_digital(b, a, wd)) + 1e-300)
                        rb, ra = rbj_blt(kind, f0, gdb, Q, fs)
                        r = 20*np.log10(np.abs(H_digital(rb, ra, wd)) + 1e-300)
                        mask = ref > -60
                        em = np.max(np.abs(m-ref)[mask]); er = np.max(np.abs(r-ref)[mask])
                        key = (fs, kind)
                        if key not in worst or em > worst[key][0]:
                            worst[key] = (em, er, f0, gdb, Q)
                        # zeros/poles inside the unit circle
                        assert np.all(np.abs(np.roots(a)) < 1), (kind,f0,gdb,Q,fs)
                        if kind not in ('hp','lp'):
                            assert np.all(np.abs(np.roots(b)) <= 1+1e-9), ('zeros', kind,f0,gdb,Q,fs, np.abs(np.roots(b)))
    for k, v in sorted(worst.items()):
        print(k, 'matched max err %.3f dB | bilinear %.3f dB  at f0=%g g=%g Q=%g' % v)
    # SVF realisation equals the biquad
    fs = 48000
    for kind, f0, gdb, Q in (('bell', 30, 6, 2), ('highshelf', 12000, -4, 0.7), ('lp', 18000, 0, 0.71), ('bell', 15000, 9, 1)):
        b, a = matched(kind, f0, gdb, Q, fs)
        x = np.zeros(4096); x[0] = 1
        h = svf_run(x, *svf_from_biquad(b, a))
        from scipy.signal import lfilter
        h2 = lfilter(b, a, x)
        print('svf vs biquad', kind, np.max(np.abs(h-h2)))
