// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_CLIPBOARD_MERGEDSELECTIONEXTRACTOR_H
#define RUWA_FEATURES_CANVAS_CLIPBOARD_MERGEDSELECTIONEXTRACTOR_H

#include "shared/imaging/PixelSurface.h"
#include "shared/tiles/TileGrid.h"

#include <QRect>

#include <memory>

namespace aether {
struct MergedSelectionExtraction {
    std::unique_ptr<TileGrid> pixels;
    QRect contentBounds;

    explicit operator bool() const { return pixels != nullptr && !contentBounds.isEmpty(); }
};

/// Pixel-exact bounds of non-zero selection coverage, including soft edges.
bool selectionMaskPixelBounds(const TileGrid& selectionMask, QRect& outBounds);

/// Apply an RGBA8 selection mask to a premultiplied composited surface.
/// @p surfaceBounds gives the surface's document-space placement. The returned
/// tiles keep those document coordinates for an in-place clipboard paste.
MergedSelectionExtraction extractMergedSelectionPixels(
    const ruwa::shared::imaging::PixelSurface& composite, const QRect& surfaceBounds,
    const TileGrid& selectionMask, TilePixelFormat outputFormat);

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_CLIPBOARD_MERGEDSELECTIONEXTRACTOR_H
