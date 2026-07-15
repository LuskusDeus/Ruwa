# SPDX-License-Identifier: MPL-2.0

# =============================================================================
#  Independent spectral pigment-mixing prototype (public-domain data only).
#
#  Goal of this phase-1 script: prove that a spectral Kubelka-Munk model built
#  ONLY from public-domain data reproduces subtractive paint mixing
#  (blue + yellow -> green, not gray) with an exact sRGB round-trip. This model
#  replaced an earlier Mixbox-based prototype; see the "Pigment colour
#  mixing" section of THIRD_PARTY_NOTICES.md for the provenance record.
#
#  The analytic model itself uses no lookup texture and no external polynomial
#  coefficients. (The shipped build precomputes a LUT from this same model for
#  speed via tools/pigment-lut-generator; the LUT is derived data, not input.)
#  Its inputs are:
#    * CIE 1931 2-deg color matching functions  -> Wyman/Sloan/Shirley 2013
#      analytic multi-lobe Gaussian fit (published formulas, no table).
#    * CIE Standard Illuminant D65 relative SPD  -> standard CIE table.
#    * sRGB (IEC 61966-2-1) transfer + XYZ matrix -> the standard.
#
#  Reflectance reconstruction uses the "least slope squared" (LSS) smooth-
#  metamer method (Scott Allen Burns, public): find the smoothest reflectance
#  that reproduces the target color. Because the color constraint is linear,
#  the reconstruction collapses to a single precomputed matrix W (n x 3).
#
#  Pipeline per color:
#     sRGB -> linear -> reflectance rho = clamp(W @ lin)           (encode)
#     mix:  K/S = (1-rho)^2 / (2 rho);  KS_mix = sum_i w_i KS_i;
#           rho_mix = 1 + KS - sqrt(KS^2 + 2 KS)                   (KM mix)
#     reflectance -> linear = A @ rho -> sRGB                       (decode)
#
#  A "residual" (target - decode(encode(target)) in linear space) is carried
#  alongside rho so the round-trip is bit-exact and mixing identical colors is
#  the identity, matching the contract used by the GLSL implementation.
# =============================================================================
import numpy as np

# ---- wavelength grid --------------------------------------------------------
LAMBDA = np.arange(380.0, 731.0, 10.0)      # 380..730 nm, 36 bins
N = LAMBDA.size

# ---- CIE 1931 2-deg CMF: Wyman/Sloan/Shirley 2013 analytic fit ---------------
def _gauss(x, mu, s1, s2):
    # piecewise Gaussian: inverse-width s1 below the peak, s2 above
    t = (x - mu) * np.where(x < mu, s1, s2)
    return np.exp(-0.5 * t * t)

def _cmf(lam):
    xbar = (1.056 * _gauss(lam, 599.8, 0.0264, 0.0323)
            + 0.362 * _gauss(lam, 442.0, 0.0624, 0.0374)
            - 0.065 * _gauss(lam, 501.1, 0.0490, 0.0382))
    ybar = (0.821 * _gauss(lam, 568.8, 0.0213, 0.0247)
            + 0.286 * _gauss(lam, 530.9, 0.0613, 0.0322))
    zbar = (1.217 * _gauss(lam, 437.0, 0.0845, 0.0278)
            + 0.681 * _gauss(lam, 459.0, 0.0385, 0.0725))
    return xbar, ybar, zbar

XBAR, YBAR, ZBAR = _cmf(LAMBDA)             # each length N

# ---- CIE Standard Illuminant D65 relative SPD (380..730 / 10nm) --------------
D65 = np.array([
    49.98, 54.65, 82.75, 91.49, 93.43, 86.68, 104.87, 117.01, 117.81, 114.86,
    115.92, 108.81, 109.35, 107.80, 104.79, 107.69, 104.41, 104.05, 100.00,
    96.33, 95.79, 88.69, 90.01, 89.60, 87.70, 83.29, 83.70, 80.03, 80.21,
    82.28, 78.28, 69.72, 71.61, 74.35, 61.60, 69.89,
])
assert D65.size == N, (D65.size, N)

# ---- sRGB <-> XYZ (D65) ------------------------------------------------------
M_XYZ2RGB = np.array([
    [ 3.2406255, -1.5372080, -0.4986286],
    [-0.9689307,  1.8757561,  0.0415175],
    [ 0.0557101, -0.2040211,  1.0569959],
])

def srgb_to_linear(c):
    c = np.asarray(c, float)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

def linear_to_srgb(c):
    c = np.clip(np.asarray(c, float), 0.0, 1.0)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * c ** (1.0 / 2.4) - 0.055)

# ---- reflectance -> linear sRGB operator A (3 x N) ---------------------------
# XYZ = k * sum_lambda CMF * D65 * rho ;  k normalizes a perfect diffuser
# (rho = 1) to Y = 1 (i.e. white).
_w = YBAR * D65
_k = 1.0 / _w.sum()
_T = _k * np.vstack([XBAR * D65, YBAR * D65, ZBAR * D65])   # 3 x N : rho -> XYZ
A = M_XYZ2RGB @ _T                                          # 3 x N : rho -> lin sRGB

# ---- LSS reflectance reconstruction: W (N x 3), lin sRGB -> reflectance ------
# Minimize ||D2 rho||^2 (smoothness) subject to A rho = target_linear.
# Second-difference operator (n-2 x n); ridge keeps H invertible.
_D2 = (np.eye(N)[:-2] - 2.0 * np.eye(N)[1:-1] + np.eye(N)[2:])
_H = _D2.T @ _D2 + 1e-8 * np.eye(N)
_Hinv = np.linalg.inv(_H)
_AHiAt_inv = np.linalg.inv(A @ _Hinv @ A.T)
W = _Hinv @ A.T @ _AHiAt_inv                               # N x 3 ; A @ W == I

EPS = 1e-4

def reconstruct_reflectance(lin_rgb):
    rho = W @ np.asarray(lin_rgb, float)
    return np.clip(rho, EPS, 1.0)

def reflectance_to_linear(rho):
    return A @ rho

# ---- pigment latent: reflectance + linear-space residual --------------------
class Latent:
    __slots__ = ("rho", "res")
    def __init__(self, rho, res):
        self.rho = rho
        self.res = res

def encode(rgb):
    lin = srgb_to_linear(rgb)
    rho = reconstruct_reflectance(lin)
    res = lin - reflectance_to_linear(rho)      # absorbs clamp / gamut error
    return Latent(rho, res)

def _km_forward(rho):
    return (1.0 - rho) ** 2 / (2.0 * rho)       # reflectance -> K/S

def _km_inverse(ks):
    return 1.0 + ks - np.sqrt(ks * ks + 2.0 * ks)   # K/S -> reflectance

def mix_latents(latents, weights):
    w = np.asarray(weights, float)
    w = w / w.sum()
    ks = sum(wi * _km_forward(l.rho) for wi, l in zip(w, latents))
    rho = _km_inverse(ks)
    res = sum(wi * l.res for wi, l in zip(w, latents))
    return Latent(rho, res)

def decode(latent):
    lin = reflectance_to_linear(latent.rho) + latent.res
    return linear_to_srgb(np.clip(lin, 0.0, 1.0))

def mix(rgb1, rgb2, t):
    return decode(mix_latents([encode(rgb1), encode(rgb2)], [1.0 - t, t]))

# =============================================================================
#  Validation
# =============================================================================
def _hue_name(rgb):
    r, g, b = rgb
    mx = max(rgb); mn = min(rgb)
    if mx - mn < 0.06:
        return "gray"
    if g >= r and g >= b:
        return "GREEN"
    if r >= g and r >= b:
        return "orange/red" if g > b else "red/magenta"
    return "blue/violet"

if __name__ == "__main__":
    np.set_printoptions(precision=4, suppress=True)

    # -- [1] round-trip exactness --------------------------------------------
    test_cols = [(1,0,0),(0,1,0),(0,0,1),(1,1,0),(0,1,1),(1,0,1),
                 (1,1,1),(0,0,0),(0.5,0.5,0.5),(0.2,0.6,0.9),(0.8,0.1,0.3)]
    worst_rt = 0.0
    for c in test_cols:
        back = decode(encode(c))
        worst_rt = max(worst_rt, float(np.max(np.abs(back - np.array(c)))))
    print(f"[1] round-trip worst abs error (0..1): {worst_rt:.2e}  "
          f"-> {worst_rt*255:.3f}/255")

    # -- [2] identity mix -----------------------------------------------------
    c = np.array([0.2, 0.6, 0.9])
    idm = mix(c, c, 0.5)
    print(f"[2] mix(c,c,0.5) error: {float(np.max(np.abs(idm-c))):.2e}")

    # -- [3] the money shot: blue + yellow at t=0.5 ---------------------------
    blue = (0.0, 0.129, 0.522)      # cobalt blue-ish
    yellow = (0.988, 0.827, 0.0)    # hansa yellow-ish
    mid = mix(blue, yellow, 0.5)
    print(f"[3] blue{blue} + yellow{yellow} @0.5 = "
          f"{tuple(round(v,3) for v in mid)}  -> {_hue_name(mid)}")
    rgb_lerp = tuple(0.5*np.array(blue)+0.5*np.array(yellow))
    print(f"    (naive RGB lerp would be {tuple(round(v,3) for v in rgb_lerp)} "
          f"-> {_hue_name(rgb_lerp)})")

    # -- [4] classic pigment pairs, midpoints ---------------------------------
    pairs = {
        "cad.red + cobalt.blue":  ((1.0,0.153,0.008), (0.0,0.129,0.522)),
        "magenta + yellow":       ((0.502,0.008,0.180),(0.996,0.925,0.0)),
        "phthalo.blue + yellow":  ((0.051,0.106,0.267),(0.988,0.827,0.0)),
        "white + ultramarine":    ((1.0,1.0,1.0),(0.098,0.0,0.349)),
        "black + white":          ((0,0,0),(1,1,1)),
        "green + red":            ((0.027,0.427,0.086),(1.0,0.153,0.008)),
    }
    print("[4] midpoint mixes:")
    for name, (a, b) in pairs.items():
        m = mix(a, b, 0.5)
        print(f"    {name:26s} -> {tuple(round(v,3) for v in m)}  {_hue_name(m)}")

    # -- [5] optional: render a comparison grid PNG ---------------------------
    try:
        from PIL import Image
        rows = list(pairs.values()) + [(blue, yellow)]
        steps = 24
        sw, sh = 40, 48
        img = Image.new("RGB", (steps*sw, len(rows)*sh))
        px = img.load()
        for r, (a, b) in enumerate(rows):
            for s in range(steps):
                t = s/(steps-1)
                col = tuple(int(round(255*v)) for v in mix(a, b, t))
                for yy in range(sh):
                    for xx in range(sw):
                        px[s*sw+xx, r*sh+yy] = col
        out = r"scripts\pigment_mix_preview.png"
        img.save(out)
        print(f"[5] wrote gradient preview -> {out}")
    except Exception as e:
        print(f"[5] preview skipped: {e}")
