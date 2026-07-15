// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_EFFECTS_LAYEREFFECTTYPES_H
#define RUWA_CORE_EFFECTS_LAYEREFFECTTYPES_H

#include "shared/tiles/TileTypes.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariant>
#include <QVariantMap>
#include <QtGlobal>

#include <functional>
#include <unordered_set>

namespace ruwa::core::effects {

using EffectTileCoverage = std::unordered_set<aether::TileKey, aether::TileKeyHash>;

enum class LayerSourcePurpose {
    RawContent,
    EffectedContent,
    FinalLayerColor,
    MaskColor,
    AlphaMask,
    BoardRawContent
};

enum class EffectEvaluationSpace { DocumentTile, ViewportScreen };

/// Maps the region currently being rendered (a tile, a padded neighbourhood, or
/// the whole viewport) back to absolute DOCUMENT pixel coordinates, so an effect
/// can be a function of WHERE a fragment sits in the document rather than only of
/// the source colour. Without this a procedural / positional effect (gradient
/// overlay, vignette, pattern) restarts per tile and seams at tile borders.
///
///   documentX = originX + fragTexCoord.x * outputWidth  * documentPxPerTexel
///   documentY = originY + fragTexCoord.y * outputHeight * documentPxPerTexel
///
/// documentPxPerTexel is 1.0 in document-tile space (a texel == a document
/// pixel). `valid` is false where the mapping is not yet known (e.g. viewport
/// preview before the view origin is wired); positional effects must no-op or
/// fall back when it is false.
struct EffectRegionFrame {
    float originX = 0.0f;
    float originY = 0.0f;
    float documentPxPerTexel = 1.0f;
    bool valid = false;

    /// When true, the fragment->document mapping is the full 2D affine below
    /// instead of the axis-aligned `origin + fragTexCoord * outputSize *
    /// documentPxPerTexel`. The document-tile / whole-layer paths leave this
    /// false (their mapping is a pure translate+uniform-scale, so the basis is
    /// derived from the pass's output size). The VIEWPORT/screen path sets it,
    /// filling the basis from the camera transform so a rotated / flipped /
    /// zoomed preview still maps a screen fragment to the right document pixel —
    /// which is what lets positional & distortion effects render live in the
    /// transform / lasso / fill preview.
    ///
    ///   documentPos = origin + fragTexCoord.x * basisX + fragTexCoord.y * basisY
    ///
    /// basisX / basisY are the document-pixel vectors spanned by the full
    /// fragTexCoord.x / .y range [0,1] (so they already fold in the output size).
    bool useAffine = false;
    float basisXx = 0.0f; ///< d(documentPos)/d(fragTexCoord.x), x component
    float basisXy = 0.0f; ///< d(documentPos)/d(fragTexCoord.x), y component
    float basisYx = 0.0f; ///< d(documentPos)/d(fragTexCoord.y), x component
    float basisYy = 0.0f; ///< d(documentPos)/d(fragTexCoord.y), y component
};

struct LayerEffectState {
    QUuid instanceId = QUuid::createUuid();
    QString typeId;
    quint32 version = 1;
    bool enabled = true;
    bool realtimePreviewEnabled = true;
    bool uiExpanded = true;
    QVariantMap params;
};

/// Output tile offset relative to a source tile that may contribute pixels to
/// it. This is the common coverage model for effects whose bounds are a
/// dilation/sweep of the source coverage.
struct EffectCoverageTileOffset {
    int dx = 0;
    int dy = 0;
};

enum class EffectParamType { Bool, Int, Real, Color, Choice };

/// UI hint for Int/Real params: which editor widget the panel should build.
/// A range slider reads best when the value's position within [min,max] is
/// itself meaningful (an amount, an angle, a fraction); a typed number field
/// reads better when it doesn't (e.g. an absolute document-pixel coordinate,
/// where "halfway across a 16384px range" means nothing to the user).
enum class EffectParamEditorHint { Slider, NumberField };

/// Which half of a combined on-canvas position editor a Real param supplies —
/// see EffectParamDefinition::positionPairKey.
enum class EffectParamPositionAxis { X, Y };

/// Ties a Real param's initial value to the document's own size instead of a
/// fixed literal, so e.g. a "goes from one corner to the other" effect starts
/// out actually spanning the canvas. Only applied when the canvas has finite
/// bounds (an infinite canvas has no width/height to bind to, so `defaultValue`
/// is used as-is).
enum class EffectParamDefaultBinding {
    None,
    CanvasWidth,
    CanvasHeight,
    CanvasHalfWidth,
    CanvasHalfHeight
};

struct EffectParamDefinition {
    QString key;
    QString label;
    EffectParamType type = EffectParamType::Real;
    QVariant defaultValue;
    QVariant minimumValue;
    QVariant maximumValue;
    QVariant stepValue;
    QStringList choices;
    /// Int/Real only; ignored for other types. Defaults to Slider, matching
    /// every effect authored before this hint existed.
    EffectParamEditorHint preferredEditor = EffectParamEditorHint::Slider;
    /// Non-empty pairs this Real param up with exactly one other Real param
    /// sharing the same key (one X, one Y): the panel renders both as a single
    /// PositionInputField capsule instead of two separate number rows, and
    /// lets the user click a point directly on canvas instead of typing X/Y by
    /// hand. The X param's label is used as the combined row's label (e.g.
    /// "Start Pos"); the Y param's label is unused but kept for clarity /
    /// fallback if the pairing is ever malformed.
    QString positionPairKey;
    /// Which half of the pair this param supplies. Only meaningful when
    /// positionPairKey is non-empty.
    EffectParamPositionAxis positionAxis = EffectParamPositionAxis::X;
    /// When set and the canvas has finite bounds, overrides defaultValue with
    /// the document's width/height at the moment the effect is added.
    EffectParamDefaultBinding defaultBinding = EffectParamDefaultBinding::None;
};

struct EffectCapabilities {
    bool supportsDocumentTile = true;
    bool supportsViewportScreen = true;
    bool expandsBounds = false;
    bool requiresNeighborTiles = false;
    bool requiresBackdrop = false;
    bool orderDependent = true;
    /// Distortion class: the effect samples the layer at arbitrary, potentially
    /// far-away positions (twirl, wave, polar, displace, spherize), so a bounded
    /// neighbour-padding source cannot feed it. The compositor instead
    /// materialises the whole layer (its populated-tile bbox, dilated by the
    /// declared pixelExpansionRadius and clamped to a VRAM cap) into ONE texture,
    /// runs the entire chain on it, then crops the output tiles — see
    /// GLLayerEffectRenderContext::wholeLayerSource. pixelExpansionRadius is
    /// reinterpreted as the maximum OUTPUT displacement (drives coverage / dirty /
    /// the materialised ring), not a neighbour-gather reach. Whole-layer effects
    /// should also set requiresNeighborTiles so coverage/dirty expansion and the
    /// beyond-cap fallback (bounded-displacement neighbourhood path) size
    /// correctly. requiresBackdrop is unsupported alongside this in v1 (the
    /// materialised source has no per-tile backdrop) and falls back per-tile.
    bool readsWholeLayer = false;
};

struct EffectPlan {
    QString typeId;
    EffectEvaluationSpace space = EffectEvaluationSpace::DocumentTile;
    QVariantMap params;
};

struct LayerEffectDescriptor {
    QString typeId;
    QString displayName;
    /// Picker folder this effect lives under (e.g. "Blur", "Color Adjust").
    /// Empty falls back to the catalog's "Other" bucket.
    QString category;
    quint32 version = 1;
    EffectCapabilities capabilities;
    QList<EffectParamDefinition> params;
    /// Legacy tile-granularity expansion. Still honoured, but prefer
    /// pixelExpansionRadius which the renderer uses to size the padded
    /// neighbour source so the effect can read across tile borders.
    std::function<int(const LayerEffectState&)> tileExpansionRadius;
    /// Pixel-space radius the effect samples beyond a tile's own bounds
    /// (e.g. a blur/shadow kernel). This is the renderer's scalar neighbour
    /// padding. Coverage uses coverageResolver/coverageTileOffsets when present
    /// and falls back to this radius only for legacy/simple effects. Coverage
    /// fallback requires capabilities.expandsBounds; renderer padding requires
    /// capabilities.requiresNeighborTiles.
    std::function<int(const LayerEffectState&)> pixelExpansionRadius;
    /// Fine-grained document-tile coverage callback. Use this when an effect's
    /// output coverage is not a simple source-coverage expansion by tile radius
    /// (directional blur, displacement, future deformation effects). The
    /// callback receives the current chain coverage and must return the next
    /// coverage; if omitted, EffectCoverageResolver falls back to
    /// coverageTileOffsets, then pixelExpansionRadius, then tileExpansionRadius.
    std::function<EffectTileCoverage(const LayerEffectState&, const EffectTileCoverage&)>
        coverageResolver;
    /// Fine-grained dilation/sweep footprint for output coverage. Offsets are
    /// applied to every input tile and unioned with the input coverage. This is
    /// cheaper to author than coverageResolver for effects that only add bleed.
    std::function<QList<EffectCoverageTileOffset>(const LayerEffectState&)> coverageTileOffsets;
};

inline bool operator==(const LayerEffectState& lhs, const LayerEffectState& rhs)
{
    return lhs.instanceId == rhs.instanceId && lhs.typeId == rhs.typeId
        && lhs.version == rhs.version && lhs.enabled == rhs.enabled
        && lhs.realtimePreviewEnabled == rhs.realtimePreviewEnabled
        && lhs.uiExpanded == rhs.uiExpanded && lhs.params == rhs.params;
}

inline bool operator!=(const LayerEffectState& lhs, const LayerEffectState& rhs)
{
    return !(lhs == rhs);
}

// === Effect picker catalog ===
//
// The add-effect popup renders a folder tree: category headers with effect
// leaves underneath. Some leaves are real, registered effects (usable now);
// others are planned placeholders shown greyed-out so the taxonomy reads as a
// complete map of what the editor will eventually offer.

/// Item kinds the picker knows how to render.
enum class EffectPickerItemKind {
    Category, ///< A collapsible folder header.
    Effect ///< A selectable (or placeholder) effect leaf.
};

/// A single effect leaf inside a picker category.
struct EffectCatalogEntry {
    QString typeId; ///< Registered type id, or empty for a placeholder.
    QString displayName;
    bool implemented = false; ///< true = can be applied now; false = "coming soon".
};

/// A picker folder plus its effect leaves, in display order.
struct EffectCatalogCategory {
    QString name;
    QList<EffectCatalogEntry> entries;

    int implementedCount() const
    {
        int n = 0;
        for (const auto& e : entries) {
            if (e.implemented) {
                ++n;
            }
        }
        return n;
    }
};

} // namespace ruwa::core::effects

#endif // RUWA_CORE_EFFECTS_LAYEREFFECTTYPES_H
