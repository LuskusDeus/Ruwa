// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A R E D   |   I M A G E   R E S A M P L E R
// ==========================================================================
//
//   Separable, alpha-correct, filter-selectable image resampling.
//
//   What makes this the real thing rather than a QImage::scaled() wrapper:
//
//     1. The kernel WIDENS on minification. A fixed-radius filter applied to a
//        4x downscale reads 1 source pixel in 4 and aliases; scaling the
//        kernel's support by 1/ratio turns the same filter into a correct
//        low-pass. This is the single difference that separates a professional
//        downscale from a broken one, and Qt's Fast/Smooth pair does not
//        expose it.
//     2. Filtering happens on PREMULTIPLIED alpha. Averaging straight-alpha
//        color pulls the color of fully transparent pixels (which is
//        arbitrary) into visible edges — the classic dark or white halo around
//        a cut-out. A straight source is converted in and back out; a source
//        that is ALREADY premultiplied is filtered as-is and stays that way,
//        so a GPU readback is never divided and re-multiplied just to pass
//        through here.
//     3. Both passes run in float regardless of the surface storage, so an
//        8-bit source does not quantize between the horizontal and the
//        vertical pass.
//     4. The two passes are BANDED with a sliding cache of horizontally
//        filtered rows. A naive separable implementation materializes a
//        dstW x srcH float intermediate — 1.1 GB for a 6000px document scaled
//        2x. Here peak scratch is bounded by a budget, each source row is
//        still filtered exactly once, and the band boundary is where progress
//        is reported and cancellation is polled.
//
//   Color space: filtering is done on the values as stored, which for this
//   application means sRGB-encoded, NOT linear light. That is deliberate. The
//   engine composites in the same encoded space, so resampling there keeps an
//   exported image consistent with what the canvas showed; linearizing here
//   alone would make a downscaled export disagree with its own preview.
//

#ifndef RUWA_SHARED_IMAGING_IMAGERESAMPLER_H
#define RUWA_SHARED_IMAGING_IMAGERESAMPLER_H

#include "shared/imaging/PixelSurface.h"

#include <QSize>

#include <cstdint>
#include <functional>

namespace ruwa::shared::imaging {

enum class ResampleFilter : uint8_t {
    /// Point sampling. The only filter that does NOT widen on minification —
    /// widening it would silently turn it into a box filter, and a user who
    /// picks Nearest is asking for hard pixel edges (pixel art, masks).
    Nearest = 0,
    /// Triangle, radius 1. Cheap, slightly soft.
    Bilinear = 1,
    /// Catmull-Rom cubic, radius 2. The neutral default: mild edge
    /// sharpening, small overshoot.
    Bicubic = 2,
    /// Mitchell-Netravali (B=C=1/3), radius 2. Softer than Bicubic, no
    /// visible ringing — the safe choice for photographic content.
    Mitchell = 3,
    /// Lanczos, radius 3. Sharpest; can ring on hard edges.
    Lanczos3 = 4,
};

[[nodiscard]] const char* resampleFilterName(ResampleFilter filter);

struct ResampleOptions {
    ResampleFilter filter = ResampleFilter::Bicubic;

    /// Peak bytes the banded intermediate may occupy. A single output row can
    /// exceed it when the minification ratio is extreme — that case has no
    /// smaller formulation, so the budget is a target, not a hard cap.
    qsizetype scratchBudgetBytes = 64 * 1024 * 1024;
};

/// Progress in [0,1]; called at band boundaries, never from more than one
/// thread at a time. `shouldCancel` is polled at the same points: returning
/// true abandons the work and yields a null surface.
struct ResampleHooks {
    std::function<void(qreal)> onProgress;
    std::function<bool()> shouldCancel;
};

/// Resample `src` to exactly `targetSize`. The result keeps the source's
/// storage. Returns a null surface on invalid input, allocation failure, or
/// cancellation — callers distinguish those through the hooks they passed.
[[nodiscard]] PixelSurface resample(const PixelSurface& src, QSize targetSize,
    const ResampleOptions& options = {}, const ResampleHooks& hooks = {});

} // namespace ruwa::shared::imaging

#endif // RUWA_SHARED_IMAGING_IMAGERESAMPLER_H
