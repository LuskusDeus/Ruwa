// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A R E D   |   C A N V A S   W I D G E T S
// ==========================================================================

#ifndef RUWA_SHARED_TYPES_CANVASWIDGETS_H
#define RUWA_SHARED_TYPES_CANVASWIDGETS_H

#include <array>
#include <cstddef>

namespace ruwa::ui {

/**
 * @brief A draggable widget living on top of the canvas.
 *
 * These are the widgets the user toggles individually from View → Canvas
 * widgets, and whose visibility is persisted per workspace. Everything that
 * used to be a bool-per-widget parameter, member or signal takes this enum
 * plus a bool instead.
 */
enum class CanvasWidget {
    Joystick, ///< CanvasStylusJoystickContainerWidget
    BrushControl, ///< BrushControlOverlay
    ToolState, ///< CanvasToolStateOverlay ("Tool bar" in the menu)
};

/// Every canvas widget, in menu order. Iterate this instead of repeating a
/// statement per widget.
inline constexpr std::array kCanvasWidgets {
    CanvasWidget::Joystick,
    CanvasWidget::BrushControl,
    CanvasWidget::ToolState,
};

/**
 * @brief Visibility of all canvas widgets as one value.
 *
 * Passed and stored whole so that callers who care about the whole set (menu
 * sync, save/restore, presets) do not spell out one bool per widget, while
 * callers who touch a single widget index it with a CanvasWidget.
 */
class CanvasWidgetVisibility {
public:
    constexpr CanvasWidgetVisibility() = default;

    constexpr bool operator[](CanvasWidget widget) const { return m_visible[indexOf(widget)]; }
    constexpr bool& operator[](CanvasWidget widget) { return m_visible[indexOf(widget)]; }

    /// True when no widget is hidden (i.e. the default state).
    constexpr bool allVisible() const
    {
        for (const bool visible : m_visible) {
            if (!visible)
                return false;
        }
        return true;
    }

    friend constexpr bool operator==(const CanvasWidgetVisibility&, const CanvasWidgetVisibility&)
        = default;

private:
    static constexpr std::size_t indexOf(CanvasWidget widget)
    {
        return static_cast<std::size_t>(widget);
    }

    std::array<bool, kCanvasWidgets.size()> m_visible { true, true, true };
};

} // namespace ruwa::ui

#endif // RUWA_SHARED_TYPES_CANVASWIDGETS_H
