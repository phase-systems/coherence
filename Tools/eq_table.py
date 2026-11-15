from eq_design import *
import sys
f = np.geomspace(20, 20000, 1500)
def err(kind, f0, g, Q, fs, design):
    num, den = proto(kind, f0, g, Q)
    ref = 20*np.log10(np.abs(H_analog(num, den, 2*np.pi*f)))
    b, a = design(kind, f0, g, Q, fs)
    m = 20*np.log10(np.abs(H_digital(b, a, 2*np.pi*f/fs)) + 1e-300)
    mask = ref > -24
    return np.max(np.abs(m-ref)[mask])
rows = []
for kind, Qs, gains, f0s in (('bell', (0.3,0.7,1.5,4,10), (-15,-6,-2,2,6,15), (1000,4000,8000,12000,16000,20000)),
                             ('highshelf', (0.5,0.71,1.0), (-15,-6,6,15), (1000,4000,8000,12000,16000,20000)),
                             ('lowshelf', (0.5,0.71,1.0), (-15,-6,6,15), (20,100,500,1000)),
                             ('hp', (0.5,0.71,1.0,2.0), (0,), (10,30,100,500,1000)),
                             ('lp', (0.5,0.71,1.0,2.0), (0,), (2000,8000,12000,16000,20000))):
    for f0 in f0s:
        line = [kind, f0]
        for fs in (44100, 48000, 96000):
            em = max(err(kind,f0,g,Q,fs,matched) for g in gains for Q in Qs)
            eb = max(err(kind,f0,g,Q,fs,rbj_blt) for g in gains for Q in Qs)
            line += [em, eb]
        rows.append(line)
print('%-10s %6s | %-15s | %-15s | %-15s' % ('type','f0','44.1k mat/blt','48k mat/blt','96k mat/blt'))
for r in rows:
    print('%-10s %6d | %6.2f / %6.2f | %6.2f / %6.2f | %6.2f / %6.2f' % tuple(r))
