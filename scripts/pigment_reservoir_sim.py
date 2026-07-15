# SPDX-License-Identifier: MPL-2.0

# =============================================================================
#  Phase 2 - reservoir stroke simulator for the independent pigment mixer.
#
#  Phase 1 (pigment_mix_prototype.py) proved the spectral Kubelka-Munk engine
#  in isolation. The real complaints are about its INTEGRATION with the wet
#  reservoir/alpha pipeline (GLBrushRenderer.cpp: wetReservoirUpdate +
#  kSmudgeApplyFrag):
#     (1) light tones mix "strangely" (muddy / bowed toward white);
#     (2) painting A on opaque B vs painting A on transparent then entering B
#         in the same stroke give different results (order dependence).
#
#  We reduce the 2-D brush to a 1-D moving brush with ONE reservoir cell
#  travelling across a 1-D canvas. That single stateful cell reproduces the
#  "carry" (advection) that causes order dependence, while staying tunable
#  before any GLSL.
#
#  OLD  = today's structure (bespoke init, PURE KM, KM re-mix at deposit).
#  NEW  = the 4 co-design principles:
#     P1  ONE pigment mix / dab: pickup mixes pigment, apply composites
#         OPTICALLY (linear premultiplied), no second KM mix at deposit.
#     P2  concentration space: each term weighted by pigment AMOUNT
#         (weight*alpha); transparent => 0 pigment, no divide-by-alpha, no
#         separate pen-fill-colour regime.
#     P3  tone-aware KM<->tint blend driven by HSV SATURATION of the inputs:
#         saturated pigments -> full KM (blue+yellow->green); pastels/near-grey
#         -> linear reflectance average (stay bright, no mud).
#     P4  first dab (init) uses the SAME update rule as steady state.
# =============================================================================
import importlib.util, os
import numpy as np

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "pm", os.path.join(_here, "pigment_mix_prototype.py"))
pm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pm)

def rgb_to_rho(rgb):
    return pm.reconstruct_reflectance(pm.srgb_to_linear(np.asarray(rgb, float)))

def rho_to_lin(rho):
    return pm.reflectance_to_linear(rho)

def rho_to_rgb(rho):
    return pm.linear_to_srgb(np.clip(rho_to_lin(rho), 0.0, 1.0))

def hsv_sat_rgb(rgb):
    rgb = np.clip(np.asarray(rgb, float), 0.0, 1.0)
    mx = rgb.max()
    return 0.0 if mx <= 1e-6 else float((mx - rgb.min()) / mx)

KMf = pm._km_forward
KMi = pm._km_inverse


class Paint:
    __slots__ = ("rho", "a")
    def __init__(self, rho, a):
        self.rho = rho
        self.a = float(a)

EMPTY = Paint(rgb_to_rho((1, 1, 1)), 0.0)


# ---- tone-aware pigment blend of weighted reflectance samples ----------------
#   Returns (reflectance, kmW). sat_lo/sat_hi map mean input saturation to the
#   KM<->linear crossfade; km_floor keeps a minimum KM so greys still mix a bit.
def blend_reflectance(rhos, concs, sat_lo=0.25, sat_hi=0.75, km_floor=0.0,
                      force_km=False):
    concs = np.asarray(concs, float)
    total = concs.sum()
    if total < 1e-9:
        return rhos[0], 0.0
    w = concs / total
    ks_mix = sum(wi * KMf(r) for wi, r in zip(w, rhos))
    rho_km = KMi(ks_mix)
    if force_km:
        return rho_km, 1.0
    rho_lin = sum(wi * r for wi, r in zip(w, rhos))
    sat = sum(wi * hsv_sat_rgb(rho_to_rgb(r)) for wi, r in zip(w, rhos))
    t = np.clip((sat - sat_lo) / max(sat_hi - sat_lo, 1e-6), 0.0, 1.0)
    kmW = km_floor + (1.0 - km_floor) * (t * t * (3.0 - 2.0 * t))   # smoothstep
    return (1.0 - kmW) * rho_lin + kmW * rho_km, float(kmW)


# =============================================================================
#  Reservoir update rules
# =============================================================================
class Params:
    def __init__(self, pickup=0.35, spread=0.5, dilution=0.0, deposit=0.7,
                 pen_fill_gate=1.0, sat_lo=0.25, sat_hi=0.75, km_floor=0.0):
        self.k = pickup; self.s = spread; self.d = dilution
        self.deposit = deposit; self.pen_fill_gate = pen_fill_gate
        self.sat_lo = sat_lo; self.sat_hi = sat_hi; self.km_floor = km_floor


def _weights(prev_a, canvas_a, P):
    wCan = P.k * (1.0 - P.s) * canvas_a       # already carries canvas alpha
    wPen = P.k * P.s                          # pen load == 1
    wPrev = (1.0 - wPen - wCan) * (1.0 - P.d)
    return wPrev, wCan, wPen


def update_NEW(prev, canvas, pen_rho, P):
    """P1..P4: concentration space, tone-aware, unified init (prev.a may be 0)."""
    wPrev, wCan, wPen = _weights(prev.a, canvas.a, P)
    cPrev, cCan, cPen = wPrev * prev.a, wCan, wPen        # pigment amounts
    rho_out, kmW = blend_reflectance(
        [prev.rho, canvas.rho, pen_rho], [cPrev, cCan, cPen],
        P.sat_lo, P.sat_hi, P.km_floor)
    aOut = cPrev + cCan + cPen
    aFill = min(aOut + max(P.k - wCan - wPen, 0.0) * P.pen_fill_gate, 1.0)
    return Paint(rho_out, aFill), kmW


def update_OLD(prev, canvas, pen_rho, P, is_init):
    """Today's structure: bespoke boosted-spread init, PURE KM everywhere."""
    if is_init:
        sInit = P.s + (1.0 - P.s) * (1.0 - canvas.a)
        rho, _ = blend_reflectance([canvas.rho, pen_rho],
                                   [(1.0 - sInit) * canvas.a, sInit],
                                   force_km=True)
        return Paint(rho, min(canvas.a * (1.0 - sInit) + sInit, 1.0)), 1.0
    wPrev, wCan, wPen = _weights(prev.a, canvas.a, P)
    cPrev = wPrev * prev.a
    rho, _ = blend_reflectance([prev.rho, canvas.rho, pen_rho],
                               [cPrev, wCan, wPen], force_km=True)
    aOut = cPrev + wCan + wPen
    aFill = min(aOut + max(P.k - wCan - wPen, 0.0) * P.pen_fill_gate, 1.0)
    return Paint(rho, aFill), 1.0


# ---- deposit (apply) --------------------------------------------------------
def deposit_optical(canvas, res, w):
    """NEW P1: linear premultiplied lerp toward the reservoir (no KM)."""
    a = canvas.a * (1.0 - w) + res.a * w
    lin = (rho_to_lin(canvas.rho) * canvas.a * (1.0 - w)
           + rho_to_lin(res.rho) * res.a * w)
    straight = lin / a if a > 1e-6 else np.zeros(3)
    return pm.linear_to_srgb(np.clip(straight, 0.0, 1.0)), a


def deposit_km(canvas, res, w):
    """OLD: KM colour mix at deposit (the SECOND pigment mix)."""
    a = canvas.a * (1.0 - w) + res.a * w
    if a <= 1e-6:
        return np.zeros(3), 0.0
    rho, _ = blend_reflectance([canvas.rho, res.rho],
                               [(1.0 - w) * canvas.a, w * res.a], force_km=True)
    return rho_to_rgb(rho), a


# =============================================================================
#  1-D stroke simulation -> returns deposited sRGB and reservoir sRGB per px
# =============================================================================
def simulate(canvas_rgb, canvas_a, pen_rgb, P, mode):
    pen_rho = rgb_to_rho(pen_rgb)
    n = len(canvas_a)
    res = EMPTY
    dep = np.zeros((n, 3)); resv = np.zeros((n, 3))
    for i in range(n):
        canvas = Paint(rgb_to_rho(canvas_rgb[i]), canvas_a[i])
        if mode == "NEW":
            res, _ = update_NEW(res, canvas, pen_rho, P)
            dep[i], _ = deposit_optical(canvas, res, P.deposit)
        else:
            res, _ = update_OLD(res, canvas, pen_rho, P, is_init=(i == 0))
            dep[i], _ = deposit_km(canvas, res, P.deposit)
        resv[i] = rho_to_rgb(res.rho)
    return dep, resv


def build_canvas(kind, n, B_rgb):
    if kind == "A":
        return [B_rgb] * n, np.ones(n)
    half = n // 2
    a = np.concatenate([np.zeros(half), np.ones(n - half)])
    rgb = [(1, 1, 1)] * half + [B_rgb] * (n - half)
    return rgb, a


def transient(reservoir_C, steady_rgb, thresh=5.0):
    """px after entering B until reservoir settles within thresh/255 of steady."""
    half = len(reservoir_C) // 2
    seg = reservoir_C[half:]
    bad = np.where(np.abs(seg - steady_rgb).max(axis=1) * 255.0 >= thresh)[0]
    return int(bad[-1] + 1) if bad.size else 0


# =============================================================================
if __name__ == "__main__":
    np.set_printoptions(precision=3, suppress=True)
    N = 120
    PEN = (0.988, 0.827, 0.0)      # yellow pen (A)
    B = (0.0, 0.129, 0.522)        # blue canvas (B)
    P = Params()

    print("=== (2) Order dependence: reservoir(brush-body) colour over the B "
          "region ===")
    print("    steady = reservoir far into scenario A (opaque B from x=0).")
    for mode in ("OLD", "NEW"):
        _, resvA = simulate(*build_canvas("A", N, B), PEN, P, mode)
        rgbC, aC = build_canvas("C", N, B)
        _, resvC = simulate(rgbC, aC, PEN, P, mode)
        steady = resvA[-1]
        half = N // 2
        # steady states of A and C must agree; the transient is the carry.
        steady_gap = np.abs(resvA[-1] - resvC[-1]).max() * 255
        entry_gap = np.abs(resvA[half] - resvC[half]).max() * 255
        settle = transient(resvC, steady)
        print(f"  [{mode}] steady A/C match: {steady_gap:4.1f}/255   "
              f"gap right at entry: {entry_gap:5.1f}/255   "
              f"carry settles after {settle} px   "
              f"steadyRGB={tuple(round(v,2) for v in steady)}")

    print("\n=== (1a) P1 double-mix vs single-mix: soft-edge deposit on light "
          "canvas (white-bowing) ===")
    lc = Paint(rgb_to_rho((0.75, 0.80, 0.95)), 1.0)     # light blue canvas
    lr = Paint(rgb_to_rho((0.95, 0.90, 0.55)), 1.0)     # light yellow reservoir
    for w in (0.15, 0.35, 0.6):
        opt, _ = deposit_optical(lc, lr, w)
        km, _ = deposit_km(lc, lr, w)
        print(f"  w={w:.2f}  optical(P1)={tuple(round(v,3) for v in opt)}  "
              f"KM-remix(OLD)={tuple(round(v,3) for v in km)}  "
              f"dwhite={ (km.min()-opt.min())*255:+5.1f}/255")

    print("\n=== (1b/3) Muddiness & tone-aware: midpoint mixes ===")
    pairs = [
        ("pastel-pink + pastel-cyan", (0.98, 0.78, 0.85), (0.72, 0.95, 0.93)),
        ("light-blue + light-yellow", (0.60, 0.75, 0.95), (0.95, 0.90, 0.55)),
        ("SATURATED blue + yellow",   (0.0, 0.129, 0.522), (0.988, 0.827, 0.0)),
        ("SAT red + SAT green",       (0.9, 0.05, 0.05), (0.05, 0.55, 0.1)),
    ]
    for name, c1, c2 in pairs:
        r1, r2 = rgb_to_rho(c1), rgb_to_rho(c2)
        km, _ = blend_reflectance([r1, r2], [0.5, 0.5], force_km=True)
        tone, kmW = blend_reflectance([r1, r2], [0.5, 0.5],
                                      P.sat_lo, P.sat_hi, P.km_floor)
        Ylin = float((rho_to_lin(r1)[1] + rho_to_lin(r2)[1]) / 2)
        print(f"  {name:26s} kmW={kmW:.2f}  Y linAvg={Ylin:.3f} "
              f"KM={float(rho_to_lin(km)[1]):.3f} tone={float(rho_to_lin(tone)[1]):.3f}"
              f"  -> KM={tuple(round(v,2) for v in rho_to_rgb(km))} "
              f"tone={tuple(round(v,2) for v in rho_to_rgb(tone))}")

    # ---- visuals: reservoir (top) + deposit (bottom) per mode/scenario -------
    try:
        from PIL import Image
        blocks = []
        for mode in ("OLD", "NEW"):
            for kind, label in (("A", "on opaque B"), ("C", "transparent->B")):
                dep, resv = simulate(*build_canvas(kind, N, B), PEN, P, mode)
                blocks.append((f"{mode} {label}", resv, dep))
        rh = 34
        img = Image.new("RGB", (N, len(blocks) * rh * 2))
        px = img.load()
        for bi, (_, resv, dep) in enumerate(blocks):
            for x in range(N):
                cr = tuple(int(round(255 * v)) for v in resv[x])
                cd = tuple(int(round(255 * v)) for v in dep[x])
                for yy in range(rh):
                    px[x, (bi * 2) * rh + yy] = cr
                    px[x, (bi * 2 + 1) * rh + yy] = cd
        out = os.path.join(_here, "pigment_reservoir_preview.png")
        img.save(out)
        print("\nwrote preview ->", out)
        print("each block = 2 bands: TOP reservoir(brush body), BOTTOM deposit")
        print("blocks top->bottom:", [b[0] for b in blocks])
    except Exception as e:
        print("preview skipped:", e)
