# Exact gain at f0: adjust the band-pass mix m1 of an SVF design so |H(f0)| equals the analog target.
import numpy as np

def svf_responses(g, k, w):
    z1 = np.exp(-1j * w)
    D = (1 + g * k + g * g) + (2 * g * g - 2) * z1 + (1 - g * k + g * g) * z1 * z1
    BP = g * (1 - z1 * z1) / D
    LP = g * g * (1 + z1) ** 2 / D
    return BP, LP

def exact_f0(v, w0, target_lin):
    g, k = np.exp(v[0]), np.exp(v[1])
    m0, m1, m2 = v[2], v[3], v[4]
    BP, LP = svf_responses(g, k, w0)
    A = m0 + m2 * LP
    a = abs(BP) ** 2
    b = 2 * (A * np.conj(BP)).real
    c = abs(A) ** 2 - target_lin ** 2
    disc = b * b - 4 * a * c
    if disc < 0 or a == 0:
        return v
    r1 = (-b + np.sqrt(disc)) / (2 * a)
    r2 = (-b - np.sqrt(disc)) / (2 * a)
    out = np.array(v, dtype=float)
    out[3] = r1 if abs(r1 - m1) < abs(r2 - m1) else r2
    return out
