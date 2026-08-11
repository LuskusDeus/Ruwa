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

/// Shared geometry and optics of the refracting glass bevel. The canvas GPU
/// pass and raster QWidget backdrops use these values so the same glass keeps
/// the same apparent thickness and displacement outside the canvas. The two
/// optical values are mirrored in backdrop_refract.frag.glsl, which cannot
/// include a C++ header.
inline constexpr double kGlassRefractionWidthPx = 22.0;
inline constexpr double kGlassRefractionShiftPx = 28.0;
inline constexpr int kGlassMaxRefractionShiftDevicePx = 44;
inline constexpr double kGlassIndexOfRefraction = 1.5;
inline constexpr double kGlassChromaticDispersion = 0.055;

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
