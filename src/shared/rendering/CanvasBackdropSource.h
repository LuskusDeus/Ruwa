// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_RENDERING_CANVASBACKDROPSOURCE_H
#define RUWA_SHARED_RENDERING_CANVASBACKDROPSOURCE_H

namespace ruwa::shared::rendering {

/// How far inside its rect the glass silhouette ends, in logical pixels.
///
/// Both sides of the backdrop have to agree on this or they show each other up.
/// The GPU pass needs the inset because its coverage ramp is centred on the
/// geometric edge and would otherwise reach a pixel past it, leaking blurred
/// content out around the corners, where an SDF and QPainter approximate the
/// same arc differently. Every widget layer that paints *glass* - the tint, the
/// inner shadow - then has to stop at the same line, or it stands proud of the
/// backdrop by exactly this much. The border stroke is not glass: it stays on
/// the widget rect and covers the seam.
inline constexpr double kGlassSilhouetteInsetPx = 0.75;

/// Coordinates translucent QWidget chrome with the same-frame GPU backdrop.
class ICanvasBackdropSource {
public:
    virtual ~ICanvasBackdropSource() = default;

    /// True once the GPU backdrop pipeline is ready. Consumers use an opaque
    /// fallback until then.
    virtual bool backdropAvailable() const = 0;

    /// Request a canvas frame after a consumer moved, resized or changed
    /// visibility. Region geometry is sampled immediately before rendering.
    virtual void requestBackdropUpdate() = 0;
};

} // namespace ruwa::shared::rendering

#endif // RUWA_SHARED_RENDERING_CANVASBACKDROPSOURCE_H
