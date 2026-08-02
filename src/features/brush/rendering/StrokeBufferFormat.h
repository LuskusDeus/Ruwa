// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "features/brush/rendering/WetPigmentGpuLayout.h"
#include "shared/tiles/TileBrush.h"
#include "shared/tiles/TileFormat.h"

namespace aether {

/// Storage format of the in-progress stroke buffer for a stroke about to start
/// against a target grid of `targetFormat`.
///
/// The buffer is an intermediate: it holds the accumulating dabs until the
/// stroke flattens into the layer, which always quantizes to the document
/// format. Its own storage therefore only has to be wide enough that the
/// accumulation itself does not lose information.
///
/// RGBA8 is not, for src-over dabs. Those blend into the buffer once per draw,
/// and the render target rounds the result back onto the 1/255 grid every time.
/// The attenuation term dst * (1 - srcA) is then lost per channel whenever it
/// falls below half an LSB: at a low per-write alpha the dark channels stop
/// decaying while the bright ones keep decaying, so a long run of low-flow dabs
/// drifts in hue and undershoots in alpha. It is worst exactly where the
/// per-write alpha is smallest — the soft edge of the stroke — and grows with
/// the number of writes, i.e. as spacing shrinks. Widening the buffer removes
/// the destination round-trip entirely and leaves a single quantization, the
/// dithered one that flatten already performs.
///
/// GL_MAX runs are exempt: max is idempotent and has no attenuation term, so
/// repeating it through an 8-bit target is exact. Wet keeps its own float
/// working format for the pigment pickup round-trip; blur, smudge and liquify
/// stay on the document format and manage their own scratch textures.
[[nodiscard]] inline TilePixelFormat strokeBufferFormatFor(
    const TileBrush& brush, TilePixelFormat targetFormat)
{
    if (brush.isWetMode()) {
        return wet_pigment_gpu::workingColorFormat(targetFormat);
    }
    if (targetFormat != TilePixelFormat::RGBA8) {
        return targetFormat;
    }
    if (brush.isBlurMode() || brush.isSmudgeMode() || brush.isLiquifyMode()) {
        return targetFormat;
    }
    if (brush.usesNonAccumulatingDabBlend()) {
        return targetFormat;
    }
    return TilePixelFormat::RGBA16F;
}

} // namespace aether
