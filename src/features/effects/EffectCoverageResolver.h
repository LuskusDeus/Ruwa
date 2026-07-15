// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_EFFECTS_EFFECTCOVERAGERESOLVER_H
#define RUWA_CORE_EFFECTS_EFFECTCOVERAGERESOLVER_H

#include "features/effects/LayerEffectTypes.h"
#include "shared/tiles/TileTypes.h"

#include <unordered_set>

namespace ruwa::core::effects {

class EffectCoverageResolver {
public:
    using TileKeySet = EffectTileCoverage;

    static TileKeySet expandedDocumentCoverage(
        const TileKeySet& sourceCoverage, const QList<LayerEffectState>& effects);
    static void expandDocumentCoverageInPlace(
        TileKeySet& coverage, const QList<LayerEffectState>& effects);
    /// Output tile offsets for a rectangular pixel bleed around one source tile.
    /// Margins are document pixels and are clamped to >= 0.
    static QList<EffectCoverageTileOffset> rectangularCoverageOffsets(
        int leftPixels, int topPixels, int rightPixels, int bottomPixels);
    static QList<EffectCoverageTileOffset> radiusCoverageOffsets(int radiusPixels);
    /// Output tile offsets for a line-segment sampling footprint. Reaches are
    /// document pixels along angleDegrees; negativeReach is the -direction side,
    /// positiveReach is the +direction side.
    static QList<EffectCoverageTileOffset> lineCoverageOffsets(
        float angleDegrees, int negativeReachPixels, int positiveReachPixels);
    static int tileExpansionRadius(const LayerEffectState& effect);
    /// Pixel radius a single effect reads beyond its source bounds (0 if it
    /// does not expand bounds). Source of truth for the renderer's padding.
    static int effectPixelExpansion(const LayerEffectState& effect);
    /// Total pixel padding the document-tile renderer must gather from
    /// neighbouring tiles for this chain: the sum over enabled effects that
    /// declare requiresNeighborTiles. 0 means no padded source is needed.
    /// When realtimeOnly is true, effects with realtimePreviewEnabled==false are
    /// excluded — used by realtime previews, which don't apply those effects and
    /// therefore must not expand their region (otherwise the preview overwrites
    /// the surrounding cached, effected content).
    static int neighborhoodPadPixels(
        const QList<LayerEffectState>& effects, bool realtimeOnly = false);
    /// Keeps the gathered region stable while a parameter is edited so an
    /// unchanged prefix texture remains reusable even when the edited sampling
    /// radius changes. Falls back to neighborhoodPadPixels when no numeric range
    /// is declared; non-monotonic plugin callbacks may still grow the region on
    /// demand, preserving correctness at the cost of a cache miss.
    static int stableLiveEditNeighborhoodPadPixels(const QList<LayerEffectState>& effects,
        const QUuid& editedEffectId, const QString& editedParamKey, bool realtimeOnly = false);
    static TileKeySet coverageUnion(const TileKeySet& before, const TileKeySet& after);

private:
    static QList<EffectCoverageTileOffset> coverageOffsets(const LayerEffectState& effect);
    static void expandByOffsets(
        TileKeySet& coverage, const QList<EffectCoverageTileOffset>& offsets);
};

} // namespace ruwa::core::effects

#endif // RUWA_CORE_EFFECTS_EFFECTCOVERAGERESOLVER_H
