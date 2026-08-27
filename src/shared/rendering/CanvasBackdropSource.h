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

/// Shared geometry and optics of the refracting glass. The canvas GPU pass and
/// raster QWidget backdrops use these values so the same glass keeps the same
/// apparent thickness and displacement outside the canvas. They are mirrored in
/// backdrop_composite.frag.glsl, which cannot include a C++ header - including
/// the two helpers below, so any change to the model has to be made twice.
/// How deep the lens reaches, as a fraction of half the plate's shorter side.
///
/// This is the shape of the glass itself, and the single most visible decision
/// in the whole effect. At 1.0 the height field spans the WHOLE plate: the
/// surface is flat only along the centre line and tilts continuously from there
/// out to every edge, so the backdrop is bent everywhere and squeezed hardest
/// at the rim - a lens, which is what glass panels in other tools look like.
///
/// The earlier model was a fixed-width bevel (22 logical px) rolled onto an
/// otherwise flat pane. That confines the refraction to a ring by construction,
/// leaving the middle of every panel provably undistorted no matter how the
/// strength is tuned, and it does not scale: the same ring reads as a thick
/// edge on a small capsule and as a hairline on a large panel. Sizing the lens
/// off the plate keeps the glass looking like the same material at any size.
///
/// Lower values pull the flat region outwards and shrink the tilted part back
/// towards the edge, ending at the old bevel-like look as it approaches zero.
inline constexpr double kGlassLensExtent = 1.0;

/// Diagnostic bare-optics switch for comparing refraction against a reference
/// without frost or fill. True switches off the frost pyramid AND both tints -
/// the GPU pull towards the theme surface and the widget's own overlay - so
/// what lands on screen is the refraction and nothing else. One flag controls
/// all three so the complete material is restored consistently.
inline constexpr bool kGlassBareOpticsMode = false;

/// Which way the lens gathers: true samples towards the middle of the plate,
/// false away from it.
///
/// Tracing it says inward. The viewing ray meets the convex surface and, on
/// entering the denser medium, bends towards the outward-tilted normal, so
/// inside the glass it travels towards the middle and lands short of where it
/// entered - a converging lens gathers from closer to its own axis, and the
/// content under the plate comes out magnified.
///
/// It is a flag anyway, because the reference this is being matched against may
/// not be tracing a slab at all, and the two are trivial to tell apart on
/// screen: lay the plate across a hard horizon in the backdrop. Inward pushes
/// the horizon TOWARDS the nearer edge and never shows anything from outside
/// the plate; outward drags the surroundings in and squeezes them at the rim.
inline constexpr bool kGlassGatherInward = true;

/// Largest surface tilt the lens reaches at its silhouette, as a sine.
///
/// The Snell deviation has a vertical asymptote at 90 degrees, so an unclamped
/// profile makes the last ring of pixels displace without bound: on a plate
/// thinner than the displacement the rim looks straight through and out the far
/// side, which stops being an image of anything and reads as a hard blob that
/// changes abruptly wherever the sampling direction turns - around a rounded
/// cap, most visibly. Real glass has its edge cut somewhere short of the
/// tangent anyway. Lower values widen and soften the caustic, higher ones
/// tighten it towards the singular ring.
inline constexpr double kGlassMaxTilt = 0.92;

/// Exponent of the lens profile: tilt = rim^kGlassLensFalloff, where rim runs
/// 0 at the flat centre to 1 at the silhouette.
///
/// One is the true circular section - tilt growing linearly with distance from
/// the axis - and it is what gives the plate a visible BULGE, because the
/// displacement then varies across the whole aperture instead of only at the
/// rim.
///
/// Do not raise it to sharpen the edge. The Snell term already has a vertical
/// asymptote there; multiplying it by a profile that is also flat at zero
/// double-flattens the middle, and at two the outer eighth of the lens carried
/// some ninety per cent of the displacement - the plate went straight back to
/// looking like an outline treatment with an undistorted body. The kink at the
/// centre line that an exponent smooths away is the APEX of the lens and
/// belongs there; it was only a defect back when the profile ended at the inner
/// foot of a narrow band, which is no longer the shape being described.
inline constexpr double kGlassLensFalloff = 1.0;

/// Effective thickness of the glass, as a multiple of the lens depth: the
/// displacement is this times the tangent of the Snell deviation. Dimensionless
/// on purpose - it is a property of the material, and the reach in pixels
/// follows the plate, the way it does in real glass of a given curvature.
///
/// THE FOLD THRESHOLD LIVES HERE, and it is not a matter of taste. The plate
/// maps destination depth y to source depth y + bend(y), and that mapping stays
/// monotonic only while this times the slope of the deviation stays under one.
/// The slope peaks near 1.9 at kGlassMaxTilt, so anything past ~0.53 folds
/// somewhere, and the fold spreads inwards from the rim as the value climbs.
///
/// A little folding is the point - it is the caustic. Around 1.0 it covers the
/// outer fifth of the lens and leaves the body a smooth magnification. Push it
/// well past that and the fold swallows the plate: at 2.5 the whole top half of
/// an 800x40 toolbar gathered from an eight-pixel slice of backdrop, so the
/// body had nothing left to show while the rim ran at a 70:1 gradient - "либо
/// жестко деформирует на краях до абсурда, либо просто не работает", which is
/// one symptom and not two.
///
/// The sample is displaced according to kGlassGatherInward.
inline constexpr double kGlassRefractionStrength = 1.0;

/// Ceiling on the displacement, in device pixels. Without it a large panel
/// would ask for a sample from arbitrarily far outside itself, and the capture
/// padding that has to contain it is a compile-time constant.
inline constexpr int kGlassMaxRefractionShiftDevicePx = 72;
inline constexpr double kGlassIndexOfRefraction = 1.5;
/// Chromatic spread, as a fraction of the displacement: red is sampled that
/// much short of the bend and blue that much past it. Reads as coloured fringes
/// wherever the glass bends hard, which is a large part of what makes a lens
/// look like glass rather than a warp filter. Raised from 0.055 - which was
/// low enough to be invisible - on 2026-08-20.
///
/// The GPU path integrates a continuum across this spread (see kGlassBundleSamples);
/// three fixed wavelengths at a spread this wide read as three stacked copies
/// of the backdrop rather than a fringe. WelcomeBanner's raster copy still takes
/// the three and is correspondingly cruder.
inline constexpr double kGlassChromaticDispersion = 0.60;

/// Tangent of the Snell deviation at kGlassMaxTilt - the largest the profile
/// can reach, and so the peak of the displacement in units of the thickness.
/// RECOMPUTE THIS if kGlassMaxTilt moves; it cannot be evaluated as a constant
/// expression, and being too low here shows up as a smeared rim rather than as
/// anything that looks like a mistake.
inline constexpr double kGlassPeakDeviation = 0.5566;

/// How far a sample can travel, as a multiple of the thickness: the peak
/// deviation, plus the dispersion the short-wavelength end adds on top. The
/// capture has to be padded by at least this much or the outermost fringe reads
/// CLAMP_TO_EDGE.
inline constexpr double kGlassSampleReachFactor
    = kGlassPeakDeviation * (1.0 + kGlassChromaticDispersion);

/// Depth of the lens on a plate whose shorter side is @p minDimensionPx, in
/// whatever pixels that side was measured in.
inline double glassLensDepth(double minDimensionPx)
{
    const double depth = minDimensionPx * 0.5 * kGlassLensExtent;
    return depth > 1.0 ? depth : 1.0;
}

/// Peak displacement for a lens of @p lensDepth, clamped to the padding budget.
inline double glassRefractionShift(double lensDepth, double maxShiftPx)
{
    const double shift = lensDepth * kGlassRefractionStrength;
    return shift < maxShiftPx ? shift : maxShiftPx;
}

/// Positions sampled across the ray bundle, and wavelengths sampled at each.
///
/// Both grids are UNIFORM, and that is the whole requirement. An earlier
/// version drew one wavelength per position out of a golden-ratio sequence to
/// decorrelate the two axes with fewer fetches; what it actually did was leave
/// each channel with only the three or four positions whose wavelength happened
/// to fall under that channel's response - scattered, not spread. Every channel
/// then laid down its own handful of discrete copies of the outline, in
/// different places, which is exactly how it looked: nested shrunken traces of
/// the widget with coloured edges.
///
/// So: sample the bundle uniformly, and at each position sample the spectrum
/// uniformly. The product is more fetches than the scrambled version but every
/// channel comes out with kGlassBundleSamples * (its share of the spectrum)
/// positions at even spacing, which is what turns a fold into a caustic instead
/// of into a stack of ghosts.
inline constexpr int kGlassBundleSamples = 6;
inline constexpr int kGlassSpectralSamples = 4;

/// Half-width of the refracted ray bundle, in logical pixels.
///
/// A single displaced tap treats every ray through a pixel as hitting the same
/// surface tilt. A real bevel is curved, so the bundle covering one pixel spans
/// a range of tilts and lands on a whole segment of backdrop rather than a
/// point - the image gets drawn out ALONG the surface normal, strongly where
/// the curvature is steepest. That directional smear is most of what separates
/// thick glass from a decal, and a plain displacement cannot produce it at any
/// strength. Expressed against the lens depth: the shader converts it into a
/// span of the lens profile, so a deeper lens keeps the same look.
/// Zero collapses back to the exact single-tap behaviour.
///
/// GPU only for now: the raster counterpart in WelcomeBanner still takes one
/// tap per channel.
inline constexpr double kGlassSplayWidthPx = 6.0;

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
