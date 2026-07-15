# SPDX-License-Identifier: MPL-2.0

# Decide the wavelength-bin count for the GLSL port. Fewer bins = cheaper
# shader and a friendlier representation (16 bins fit a mat4). Rebuild the
# spectral model at several bin counts and check round-trip + mixing quality.
import numpy as np

def build(lambdas):
    def gauss(x, mu, s1, s2):
        t = (x - mu) * np.where(x < mu, s1, s2)
        return np.exp(-0.5 * t * t)
    xb = (1.056*gauss(lambdas,599.8,0.0264,0.0323)+0.362*gauss(lambdas,442.0,0.0624,0.0374)
          -0.065*gauss(lambdas,501.1,0.0490,0.0382))
    yb = (0.821*gauss(lambdas,568.8,0.0213,0.0247)+0.286*gauss(lambdas,530.9,0.0613,0.0322))
    zb = (1.217*gauss(lambdas,437.0,0.0845,0.0278)+0.681*gauss(lambdas,459.0,0.0385,0.0725))
    # D65 (380..730/10) interpolated to the requested wavelengths
    d65_10 = np.array([49.98,54.65,82.75,91.49,93.43,86.68,104.87,117.01,117.81,114.86,
        115.92,108.81,109.35,107.80,104.79,107.69,104.41,104.05,100.00,96.33,95.79,
        88.69,90.01,89.60,87.70,83.29,83.70,80.03,80.21,82.28,78.28,69.72,71.61,74.35,
        61.60,69.89])
    d65 = np.interp(lambdas, np.arange(380.0,731.0,10.0), d65_10)
    M = np.array([[3.2406255,-1.5372080,-0.4986286],
                  [-0.9689307,1.8757561,0.0415175],
                  [0.0557101,-0.2040211,1.0569959]])
    w = yb*d65; k = 1.0/w.sum()
    T = k*np.vstack([xb*d65, yb*d65, zb*d65])
    A = M @ T
    n = len(lambdas)
    D2 = np.eye(n)[:-2]-2*np.eye(n)[1:-1]+np.eye(n)[2:]
    H = D2.T@D2 + 1e-8*np.eye(n)
    Hi = np.linalg.inv(H)
    W = Hi@A.T@np.linalg.inv(A@Hi@A.T)
    return A, W

def s2l(c): return np.where(c<=0.04045, c/12.92, ((c+0.055)/1.055)**2.4)
def l2s(c):
    c=np.clip(c,0,1); return np.where(c<=0.0031308,12.92*c,1.055*c**(1/2.4)-0.055)

def test(nbins):
    lam = np.linspace(380.0, 730.0, nbins)
    A, W = build(lam)
    EPS=1e-4
    enc = lambda rgb: np.clip(W@s2l(np.array(rgb,float)), EPS, 1.0)
    dec = lambda rho: l2s(np.clip(A@rho,0,1))
    KMf = lambda r:(1-r)**2/(2*r); KMi=lambda ks:1+ks-np.sqrt(ks*ks+2*ks)
    def kmmix(a,b,t=0.5):
        ra,rb=enc(a),enc(b); ks=(1-t)*KMf(ra)+t*KMf(rb); return dec(KMi(ks))
    cols=[(1,0,0),(0,1,0),(0,0,1),(1,1,0),(0,1,1),(1,0,1),(1,1,1),(0,0,0),
          (0.2,0.6,0.9),(0.8,0.1,0.3),(0.5,0.5,0.5)]
    rt=max(float(np.max(np.abs(dec(enc(c))-np.array(c)))) for c in cols)
    by=kmmix((0.0,0.129,0.522),(0.988,0.827,0.0))   # blue+yellow -> want green
    mg=kmmix((0.502,0.008,0.180),(0.996,0.925,0.0)) # magenta+yellow -> want red/orange
    print(f"  bins={nbins:2d}  round-trip max={rt*255:6.2f}/255   "
          f"blue+yellow={tuple(round(v,2) for v in by)} "
          f"(green={'Y' if by[1]>=by[0] and by[1]>=by[2] else 'n'})   "
          f"mag+yel={tuple(round(v,2) for v in mg)}")

print("bin-count sweep (want: round-trip small, blue+yellow green, mag+yellow red/orange)")
for n in (8,10,12,16,20,26,36):
    test(n)
