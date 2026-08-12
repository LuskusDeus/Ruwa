// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   T O O L   C U R S O R   I C O N S
// ==========================================================================
// Which tools get the pointer + badge GL cursor, and the badge they use.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSORICONS_H
#define RUWA_FEATURES_CANVAS_OVERLAYS_TOOLCURSORICONS_H

#include "shared/types/ToolId.h"

#include <QString>

namespace aether {

/// QRC path of the badge icon for tools that use the pointer cursor.
/// An empty string means the tool draws its own cursor (brush, eyedropper) or
/// keeps a system cursor.
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
