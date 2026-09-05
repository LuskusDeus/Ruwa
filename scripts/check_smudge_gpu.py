# SPDX-License-Identifier: MPL-2.0

"""Exercise the production smudge shaders without building the Qt application.

Requires numpy, moderngl and an OpenGL 4.5 context. Run from any directory.
"""

import json
from pathlib import Path
import re

import moderngl
import numpy as np


SOURCE = (Path(__file__).resolve().parents[1]
          / "src/features/brush/rendering/GLBrushRenderer.cpp").read_text(encoding="utf-8")


def shader(name):
    match = re.search(r"const QString\s+" + name
                      + r"\s*=\s*(.*?);\s*$", SOURCE, re.S | re.M)
    assert match, name
    tokens = re.findall(r'"(?:[^"\\]|\\.)*"|//[^\n]*|/\*.*?\*/|\bk[A-Z]\w*\b', match[1], re.S)
    return "".join(json.loads(token) if token.startswith('"') else shader(token)
                   for token in tokens if not token.startswith('/'))


def set_uniforms(program, **values):
    for name, value in values.items():
        if name in program:
            program[name].value = value


def run(pickup_rate=0.05, strength=0.8, radius=15, steps=1200, per_dab=False,
        dtype="f1", alpha=255, logical=36, content="uniform", masked=False):
    ctx = moderngl.create_standalone_context(require=450)
    print(ctx.info["GL_RENDERER"])
    vert = shader("kSmudgeBatchVert")
    pickup = ctx.program(vertex_shader=vert, fragment_shader=shader(
        "kSmudgePickupFrag" if per_dab else "kSmudgePickupBatchFrag"))
    apply = ctx.program(vertex_shader=shader("kBrushStampVert") if per_dab else vert,
                        fragment_shader=shader("kSmudgeApplyFrag" if per_dab else "kSmudgeBatchFrag"))
    pickup_vao = ctx.vertex_array(pickup, [])
    apply_vao = ctx.vertex_array(apply, [])
    # The production per-tile vertex shader targets 256x256 tiles.
    size, physical = (256 if per_dab else 96), 64
    textures = []

    def texture(side, dtype, value=None):
        tex = ctx.texture((side, side), 4, value, dtype=dtype)
        tex.filter = (moderngl.LINEAR, moderngl.LINEAR)
        tex.repeat_x = tex.repeat_y = False
        textures.append(tex)
        return tex

    np_dtype = {"f1": np.uint8, "f2": np.float16, "f4": np.float32}[dtype]
    color = np.array([97, 153, 211, 255], np.float32) * (alpha / 255)
    if dtype != "f1":
        color /= 255
    initial = np.full((size, size, 4), color, dtype=np_dtype)
    if content == "edge":
        initial[:, size // 2:] = 0
    elif content == "colors":
        initial[:, size // 2:, :3] = initial[:, size // 2:, :3][..., ::-1]
    work = [texture(size, dtype, initial.tobytes()) for _ in range(2)]
    carry = [texture(physical, "f2", np.zeros((physical, physical, 4), np.float16).tobytes())
             for _ in range(2)]
    wf = [ctx.framebuffer([tex]) for tex in work]
    rf = [ctx.framebuffer([tex]) for tex in carry]
    mask = np.full((size, size, 4), 255, np.uint8)
    if masked:
        mask[:size // 2] = 0
    mask_tex = texture(size, "f1", mask.tobytes())
    mask_tex.use(2)
    common = dict(uOriginalTexture=0, uInvTexSize=(1 / size, 1 / size),
                  uMaxValidUv=(1, 1), uReservoirHalf=logical / 2,
                  uInvReservoirPhys=(1 / physical, 1 / physical))
    set_uniforms(pickup, **common, uReservoirSrc=1, uPickupRate=pickup_rate,
                 uRoiOriginPx=(0, 0), uInvRoiSize=(1 / size, 1 / size))
    set_uniforms(apply, **common, uReservoirTexture=1, uBrushRadius=radius,
                 uBrushHardness=0.5, uBrushRoundness=1, uBrushAngleRad=0,
                 uBrushAlpha=strength, uUseMask=int(masked), uMaskTexture=2,
                 uInvMaskSize=(1 / size, 1 / size), uUseDabShapeTexture=0,
                 uQuantizeTo8Bit=int(dtype == "f1"), uTileOriginPx=(0, 0),
                 uRoiOriginPx=(0, 0), uInvRoiSize=(1 / size, 1 / size),
                 uInvTileSize=(1 / size, 1 / size), uQuadMin=(0, 0), uQuadMax=(size, size))
    wi = ri = 0
    for i in range(steps):
        center = (size / 2 + 8 * np.sin(i * 0.13), size / 2 + 8 * np.cos(i * 0.17))
        set_uniforms(pickup, uBrushCenter=center, uBrushWorldPos=center, uInit=int(i == 0),
                     uViewportSize=(logical, logical))
        work[wi].use(0)
        carry[ri].use(1)
        rf[ri ^ 1].use()
        ctx.viewport = (0, 0, logical, logical)
        pickup_vao.render(vertices=6)
        ri ^= 1
        if i == 0:
            continue
        set_uniforms(apply, uBrushCenter=center, uViewportSize=(size, size))
        if per_dab:
            # The production single-dab path copies the source before drawing:
            # discarded fragments outside the brush must retain their content.
            ctx.copy_framebuffer(wf[wi ^ 1], wf[wi])
        work[wi].use(0)
        carry[ri].use(1)
        wf[wi ^ 1].use()
        ctx.viewport = (0, 0, size, size)
        apply_vao.render(vertices=6)
        wi ^= 1
    result = np.frombuffer(work[wi].read(), np_dtype).reshape(size, size, 4)
    if content != "uniform":
        assert not np.array_equal(result, initial), "Smudge failed to transport paint"
        assert np.all(result[..., :3] <= result[..., 3:4]), "Invalid premultiplied color"
        if content == "edge":
            assert np.any(result[:, size // 2:, 3] > 0), "Paint did not enter transparency"
            assert np.any(result[:, :size // 2, 3] < initial[:, :size // 2, 3]), \
                "Smudge incorrectly locked alpha at a real transparent edge"
        else:
            assert np.array_equal(result[..., 3], initial[..., 3]), "Opaque color mix lost alpha"
            assert result[..., :3].min() >= initial[..., :3].min(), "Color mix darkened"
            assert result[..., :3].max() <= initial[..., :3].max(), "Color mix brightened"
    elif dtype == "f1":
        assert np.array_equal(result, initial), "Smudge changed a uniform area"
    else:
        assert np.max(np.abs(result - initial)) < 0.001, "Smudge lost float precision"
        assert np.array_equal(result[..., 3], initial[..., 3]), "Smudge changed uniform alpha"
    if masked:
        assert np.array_equal(result[:size // 2], initial[:size // 2]), "Smudge escaped selection"
    print("PASS", "single" if per_dab else "batch", dtype, content,
          f"alpha={alpha} radius={radius} masked={masked} dabs={steps}")
    for resource in [*wf, *rf, *textures, pickup_vao, apply_vao, pickup, apply]:
        resource.release()
    ctx.release()


if __name__ == "__main__":
    for per_dab in (False, True):
        run(per_dab=per_dab)
        run(per_dab=per_dab, radius=22, logical=48)
        run(per_dab=per_dab, alpha=128)
        run(per_dab=per_dab, dtype="f2")
        run(per_dab=per_dab, dtype="f4")
        run(per_dab=per_dab, content="colors")
        run(per_dab=per_dab, content="edge")
        run(per_dab=per_dab, content="edge", masked=True)
