// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_RENDERING_CANVASBACKDROPREGION_H
#define RUWA_SHARED_RENDERING_CANVASBACKDROPREGION_H

#include <QColor>
#include <QRectF>

// Shared vocabulary type (plan 7.16.3): declared in the neutral shared
// rendering namespace; the `aether` alias keeps the legacy renderer internals
// building unchanged.

namespace ruwa::shared::rendering {

/// One same-frame GPU backdrop-blur region, in viewport-logical coordinates
/// with a top-left origin (the interactive canvas host's logical space).
///
/// This is the shared vocabulary between the UI chrome that declares glass
/// regions and whatever engine renders them, so it lives next to
/// ICanvasBackdropSource rather than inside any concrete renderer.
struct CanvasBackdropRegion {
    QRectF rect;
    qreal cornerRadius = 0.0;
    qreal opacity = 1.0;
    /// Theme surface the frost is tinted towards. Invalid leaves it untinted.
    QColor surfaceTint;
    /// Levels of the frost pyramid to run, clamped to the renderer's own
    /// maximum; negative takes that maximum. The reach is dominated by how far
    /// down the pyramid goes, so one level below the default reads as roughly
    /// half the blur. Zero skips the pyramid and leaves the region on the
    /// captured half, which is a mild reduction rather than none at all.
    int frostLevels = -1;
    /// Multiplier on the analytic drop shadow. Zero removes the shadow, and
    /// with it the composite padding it needed.
    qreal shadowStrength = 1.0;
    /// Multiplier on how hard the lens bends what is behind it: it scales the
    /// optical thickness and the screen-space splay together, so the plate
    /// stays the same shape and only distorts less. The lens DEPTH is not
    /// scaled - that is how wide the curved edge is, not how strong it is, and
    /// moving it would change the silhouette the tint has to match.
    qreal refractionStrength = 1.0;
};

} // namespace ruwa::shared::rendering

namespace aether {

using ruwa::shared::rendering::CanvasBackdropRegion;

} // namespace aether

#endif // RUWA_SHARED_RENDERING_CANVASBACKDROPREGION_H
