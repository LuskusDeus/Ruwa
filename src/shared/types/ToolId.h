// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_TYPES_TOOLID_H
#define RUWA_SHARED_TYPES_TOOLID_H

#include <array>
#include <optional>

namespace ruwa::ui::workspace {

/**
 * @brief Stable identity of a selectable canvas tool.
 *
 * The explicit values are persisted in project files and QSettings. Existing
 * values must therefore never be reordered or reused; new tools are appended.
 */
enum class ToolId : int {
    Hand = 0,
    Brush = 1,
    Eraser = 2,
    Fill = 3,
    Eyedropper = 4,
    Lasso = 5,
    LassoFill = 6,
    SquareSelection = 7,
    CircleSelection = 8,
    Move = 9,
    RotateView = 10,
    CanvasResize = 11,
    Zoom = 12,
    ClassicFill = 13,
    Blur = 14,
    Text = 15,
    Smudge = 16,
    Liquify = 17,
    MagicWand = 18,
};

inline constexpr std::array kToolIds {
    ToolId::Hand,
    ToolId::Brush,
    ToolId::Eraser,
    ToolId::Fill,
    ToolId::Eyedropper,
    ToolId::Lasso,
    ToolId::LassoFill,
    ToolId::SquareSelection,
    ToolId::CircleSelection,
    ToolId::Move,
    ToolId::RotateView,
    ToolId::CanvasResize,
    ToolId::Zoom,
    ToolId::ClassicFill,
    ToolId::Blur,
    ToolId::Text,
    ToolId::Smudge,
    ToolId::Liquify,
    ToolId::MagicWand,
};

constexpr std::optional<ToolId> toolIdFromValue(int value)
{
    for (const ToolId tool : kToolIds) {
        if (static_cast<int>(tool) == value) {
            return tool;
        }
    }
    return std::nullopt;
}

constexpr std::optional<ToolId> toolIdFromPersistentValue(int value)
{
    return toolIdFromValue(value);
}

constexpr int persistentValueForToolId(ToolId tool)
{
    return static_cast<int>(tool);
}

} // namespace ruwa::ui::workspace

#endif // RUWA_SHARED_TYPES_TOOLID_H
