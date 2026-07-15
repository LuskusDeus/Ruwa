// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   V I E W   C O N T R O L L E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_CANVASVIEWCONTROLLER_H
#define RUWA_UI_WORKSPACE_CANVASVIEWCONTROLLER_H

#include "shared/types/Types.h"

#include <QPoint>
#include <QPointF>
#include <Qt>

class QWheelEvent;

namespace aether {
class Canvas;
class UndoManager;
class Viewport;
} // namespace aether

namespace ruwa::ui::workspace {

class CanvasPanel;

/**
 * Handles CanvasPanel viewport, camera, zoom overlay, and coordinate mapping.
 *
 * CanvasPanel still exposes the public API; this controller owns the view logic
 * so input/tools/export code can keep calling CanvasPanel without learning a new
 * object graph.
 */
class CanvasViewController {
public:
    explicit CanvasViewController(CanvasPanel* panel);

    aether::Viewport& viewport();
    const aether::Viewport& viewport() const;
    aether::Canvas& canvas();
    const aether::Canvas& canvas() const;
    aether::UndoManager* undoManagerOrNull();
    aether::UndoManager* activeUndoManagerOrNull();

    void setZoom(float zoom);
    void setZoomSmooth(float zoom);
    void zoomBy(float factor);
    void zoomAtWorldPoint(float factor, const QPointF& worldPos);
    void zoomToFit();
    void centerCanvas();
    void toggleCanvasViewFlipHorizontal();
    void toggleCanvasViewFlipVertical();

    void applyZoomLimits();
    void showZoomInfoOverlay();
    void syncZoomInfoOverlayValue();
    void positionZoomInfoOverlay();
    void updateSelectionSizeOverlay();
    void hideSelectionSizeOverlay();
    bool handleWheelZoom(QWheelEvent* event);

    aether::Vector2 mapToWorld(const QPoint& globalPos) const;
    aether::Vector2 mapToWorld(const QPointF& globalPos) const;
    aether::Vector2 mapToViewportWorld(const QPoint& globalPos) const;
    aether::Vector2 mapToViewportWorld(const QPointF& globalPos) const;
    bool isGlobalOverGlViewport(const QPoint& globalPos) const;
    bool isGlobalOverGlViewport(const QPointF& globalPos) const;
    QPointF mapWorldToPanel(const aether::Vector2& worldPos) const;

private:
    CanvasPanel* m_panel = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASVIEWCONTROLLER_H
