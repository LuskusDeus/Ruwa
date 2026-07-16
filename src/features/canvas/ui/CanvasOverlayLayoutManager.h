// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   O V E R L A Y   L A Y O U T   M A N A G E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASOVERLAYLAYOUTMANAGER_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASOVERLAYLAYOUTMANAGER_H

#include "shared/types/CanvasWidgets.h"

#include <QPoint>
#include <QSize>

#include <memory>

class QGraphicsOpacityEffect;
class QWidget;

namespace ruwa::ui::workspace {

/** Content edge inset for canvas overlays (brush strip, tool HUD, stylus joystick, etc.). */
inline constexpr int kCanvasOverlayEdgeMargin = 6;

class CanvasPanel;
class CanvasOverlayLayout;

/**
 * @brief Owns the canvas overlay layout engine and adapts it to CanvasPanel.
 *
 * Holds a CanvasOverlayLayout that does the real work (bounds, single clamp,
 * wall-collision, edge snapping, normalized tracking, animated settle). This
 * class registers the overlay widgets and exposes thin per-widget entry points
 * (default placement, resize re-layout, visibility) used across CanvasPanel.
 */
class CanvasOverlayLayoutManager {
public:
    explicit CanvasOverlayLayoutManager(CanvasPanel* panel);
    ~CanvasOverlayLayoutManager();

    /// Underlying layout engine (bounds/clamp/collision/snap/animation).
    CanvasOverlayLayout* engine() const { return m_engine.get(); }
    /// Bind the engine to the content widget and register the overlay widgets.
    /// Call once after the overlay widgets have been created.
    void attachOverlays();

    void positionBrushOverlayDefault();
    void positionStylusJoystickDefault();
    void repositionStylusJoystickOnResize(const QSize& newSize, const QSize& oldSize);
    void scheduleInitialBrushOverlayPlacement();
    void setCanvasWidgetVisible(CanvasWidget widget, bool visible);

    void layoutToolStateOverlay();
    void repositionToolStateOverlayOnResize(const QSize& newSize, const QSize& oldSize);

    /// Called when content widget is resized (from eventFilter). Updates m_lastContentSize.
    void onContentResized(const QSize& newSize, const QSize& oldSize);

private:
    /// The panel-owned objects backing one canvas widget.
    struct WidgetHandles {
        QWidget* widget = nullptr;
        QGraphicsOpacityEffect* opacity = nullptr;
    };
    WidgetHandles handlesFor(CanvasWidget widget) const;

    CanvasPanel* m_panel = nullptr;
    std::unique_ptr<CanvasOverlayLayout> m_engine;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASOVERLAYLAYOUTMANAGER_H
