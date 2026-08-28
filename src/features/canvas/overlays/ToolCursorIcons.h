// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   T O O L   C U R S O R   I C O N S
// ==========================================================================
// Which tools get a rendered cursor, which shape it takes, and the badge it
// uses. The state model lives in the renderer-neutral workspace namespace;
// the `aether` aliases below only keep the legacy overlay internals building.
// Which QRC asset a badge maps to is an engine-integration detail and lives
// in the Aether binding, not here (plan 7.12.6).
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSORICONS_H
#define RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSORICONS_H

#include "shared/types/ToolId.h"

#include <QString>

namespace ruwa::ui::workspace {

/// Shape of the rendered cursor a tool draws.
enum class ToolCursorStyle {
    None, ///< Tool draws its own cursor (brush, eyedropper) or keeps a system one.
    Pointer, ///< Plain custom-rendered pointer arrow without a tool badge.
    PointerBadge, ///< Pointer arrow with the tool's icon hanging to its lower right.
    Crosshair, ///< Plain crosshair, for tools that place a shape by its edges.
};

/// Shape-drawing selection tools aim at an exact point rather than grab
/// something under the pointer, so they get the crosshair instead of an arrow.
inline ToolCursorStyle toolCursorStyle(ToolId tool)
{
    switch (tool) {
    case ToolId::SquareSelection:
    case ToolId::CircleSelection:
        return ToolCursorStyle::Crosshair;
    case ToolId::Fill:
    case ToolId::ClassicFill:
    case ToolId::Move:
    case ToolId::MagicWand:
    case ToolId::Lasso:
    case ToolId::LassoFill:
        return ToolCursorStyle::PointerBadge;
    default:
        return ToolCursorStyle::None;
    }
}

} // namespace ruwa::ui::workspace

namespace aether {

using ruwa::ui::workspace::ToolCursorStyle;
using ruwa::ui::workspace::toolCursorStyle;

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSORICONS_H
