// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   T O O L   C U R S O R   I C O N S
// ==========================================================================
// Which tools get a GL cursor, which shape it takes, and the badge it uses.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSORICONS_H
#define RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSORICONS_H

#include "shared/types/ToolId.h"

#include <QString>

namespace aether {

/// Shape of the GL cursor a tool draws.
enum class ToolCursorStyle {
    None, ///< Tool draws its own cursor (brush, eyedropper) or keeps a system one.
    Pointer, ///< Plain custom-rendered pointer arrow without a tool badge.
    PointerBadge, ///< Pointer arrow with the tool's icon hanging to its lower right.
    Crosshair, ///< Plain crosshair, for tools that place a shape by its edges.
};

/// Shape-drawing selection tools aim at an exact point rather than grab
/// something under the pointer, so they get the crosshair instead of an arrow.
inline ToolCursorStyle toolCursorStyle(ruwa::ui::workspace::ToolId tool)
{
    using ruwa::ui::workspace::ToolId;
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

/// QRC path of the badge icon. Empty for styles that draw no badge.
inline QString toolCursorIconResource(ruwa::ui::workspace::ToolId tool)
{
    using ruwa::ui::workspace::ToolId;
    switch (tool) {
    case ToolId::Fill:
        return QStringLiteral(":/icons/SmartFillColor");
    case ToolId::ClassicFill:
        return QStringLiteral(":/icons/FillColor");
    case ToolId::Move:
        return QStringLiteral(":/icons/Move");
    case ToolId::MagicWand:
        return QStringLiteral(":/icons/MagicWand");
    case ToolId::Lasso:
        return QStringLiteral(":/icons/Lasso");
    case ToolId::LassoFill:
        return QStringLiteral(":/icons/LassoFill");
    default:
        return QString();
    }
}

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSORICONS_H
