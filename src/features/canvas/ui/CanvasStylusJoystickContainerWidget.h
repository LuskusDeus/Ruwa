// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_WORKSPACE_CANVASSTYLUSJOYSTICKCONTAINERWIDGET_H
#define RUWA_UI_WIDGETS_WORKSPACE_CANVASSTYLUSJOYSTICKCONTAINERWIDGET_H

#include "shell/context-menu/IContextMenuProvider.h"
#include "shared/rendering/CanvasBackdropSource.h"

#include <QPoint>
#include <QRectF>
#include <QWidget>

class QMouseEvent;
class QVariantAnimation;

namespace ruwa::ui::widgets {

class CanvasStylusJoystickWidget;
class ProgressHandleSlider;

/**
 * @brief Container for stylus joystick with zoom panel below.
 *
 * Layout:
 * - Joystick (pan + rotation ring)
 * - Panel: [Zoom to fit button] [Zoom slider]
 *
 * Zoom slider is in log-space (0.01 - 64) and syncs with actual canvas zoom.
 */
class CanvasStylusJoystickContainerWidget : public QWidget, public IContextMenuProvider {
    Q_OBJECT

public:
    explicit CanvasStylusJoystickContainerWidget(QWidget* parent = nullptr);
    ~CanvasStylusJoystickContainerWidget() override;

    /// Get the joystick widget (for cursor exclusion etc.)
    CanvasStylusJoystickWidget* joystickWidget() const { return m_joystick; }
    QWidget* zoomPanelWidget() const { return m_zoomPanel; }
    bool hitTestInteractiveArea(const QPointF& pos) const;
    bool isJoystickInteractionActive() const;

    /// Update zoom slider from external zoom change (e.g. wheel zoom)
    void setZoom(qreal zoom);

    /// Sync rotation arc from external source (e.g. camera rotation from rotate-view gesture)
    void setRotation(qreal radians);

    /// Set zoom range for slider mapping (depends on canvas size)
    void setZoomLimits(qreal minZoom, qreal maxZoom);

    /// Current vertical order: joystick above zoom panel when true.
    bool isJoystickAbovePanel() const { return m_joystickAbovePanel; }

    /// Apply a persisted vertical order without triggering drag-swap logic.
    void setJoystickAbovePanel(bool above);

    /// Source coordinating the GPU blur regions for the joystick and zoom panel.
    void setBackdropSource(ruwa::shared::rendering::ICanvasBackdropSource* source);

    /// Repaint the backdrop chrome after GPU availability changes.
    void refreshBackdropContent();

    // IContextMenuProvider
    ContextMenuType contextMenuType() const override { return ContextMenuType::SimpleActions; }
    QVariantMap contextMenuContext() const override;

signals:
    /// Forwarded from joystick
    void panRequested(const QPointF& normalizedVector);
    void rotationRequested(qreal deltaRadians);

    /// Zoom panel actions
    void zoomToFitRequested();
    void zoomChangeRequested(qreal zoom);

    /// Emitted when user drags the widget to a new position
    void positionChanged(const QPoint& pos);

    /// Emitted when the user releases a widget drag (lets the layout engine settle/snap)
    void dragFinished();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onZoomButtonClicked();
    void onZoomSliderValueChanged(int value);
    void onThemeChanged();

private:
    QRectF handleRect() const;
    void handleDrag(const QPoint& globalPos);
    /// Reorder joystick/panel when the drag target's handle crosses canvas center.
    /// Returns the (possibly ±joystickH-shifted) target that keeps the handle under the cursor.
    QPoint maybeSwapForTarget(QPoint target);
    void applyLayoutOrder();
    /// Animate the joystick/panel exchanging places (children slide instead of snapping).
    void animateLayoutSwap();

    qreal sliderValueToZoom(int value) const;
    int zoomToSliderValue(qreal zoom) const;

    CanvasStylusJoystickWidget* m_joystick = nullptr;
    ProgressHandleSlider* m_zoomSlider = nullptr;
    QWidget* m_zoomPanel = nullptr; ///< Panel with handle + zoom controls
    bool m_sliderUpdateFromExternal = false;
    qreal m_minZoom = 0.8;
    qreal m_maxZoom = 58.0;

    bool m_dragging = false;
    QPoint m_dragStartPos;
    QPoint m_widgetStartPos;
    bool m_joystickAbovePanel
        = true; ///< Layout: joystick on top when true, panel on top when false
    QVariantAnimation* m_swapAnim = nullptr; ///< Active joystick/panel reorder animation, if any.

    // Backdrop-blur source (non-owning; nulled on source destruction).
    ruwa::shared::rendering::ICanvasBackdropSource* m_backdropSource = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_CANVASSTYLUSJOYSTICKCONTAINERWIDGET_H
