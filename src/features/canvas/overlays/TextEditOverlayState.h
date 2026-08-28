// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_OVERLAYS_TEXTEDITOVERLAYSTATE_H
#define RUWA_FEATURES_CANVAS_OVERLAYS_TEXTEDITOVERLAYSTATE_H

// Renderer-neutral state model for the text-edit viewport overlay
// (plan 7.28.3). The state deliberately carries DOCUMENT GEOMETRY — selection
// quads and the caret axis already mapped through the text transform by the
// UI controller — not an engine TransformState plus source-space rectangles.
// The GL renderer (TextEditOverlayGL) is a consumer of this geometry, not its
// owner: UI code publishes it through the engine presentation capability
// without importing any rendering-backend header. The `aether` alias below
// only keeps the legacy GL overlay internals building.

#include "shared/types/Types.h"

#include <array>
#include <optional>
#include <vector>

namespace ruwa::ui::workspace {

/// One selection highlight quad in document space (four corners, in the
/// order left-top, right-top, right-bottom, left-bottom).
using TextEditSelectionQuad = std::array<aether::Vector2, 4>;

struct TextEditOverlayState {
    bool active = false;
    std::vector<TextEditSelectionQuad> selectionQuads;
    bool caretVisible = false;
    /// Caret line endpoints in document space: top and bottom of the caret.
    std::optional<std::array<aether::Vector2, 2>> caretAxis;
};

} // namespace ruwa::ui::workspace

namespace aether {

using ruwa::ui::workspace::TextEditOverlayState;
using ruwa::ui::workspace::TextEditSelectionQuad;

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_OVERLAYS_TEXTEDITOVERLAYSTATE_H
