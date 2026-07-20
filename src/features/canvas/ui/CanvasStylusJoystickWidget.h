// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_WORKSPACE_CANVASSTYLUSJOYSTICKWIDGET_H
#define RUWA_UI_WIDGETS_WORKSPACE_CANVASSTYLUSJOYSTICKWIDGET_H

#include "shared/rendering/CanvasBackdropSource.h"

#include <QPointF>
#include <QRectF>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QPropertyAnimation;
class QTabletEvent;
class QTimer;
class QVariantAnimation;

namespace ruwa::ui::widgets {

class CanvasStylusJoystickWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal ringHoverProgress READ ringHoverProgress WRITE setRingHoverProgress)

public:
    explicit CanvasStylusJoystickWidget(QWidget* parent = nullptr);
    ~CanvasStylusJoystickWidget() override;

    qreal ringHoverProgress() const { return m_ringHoverProgress; }
    void setRingHoverProgress(qreal progress);
    bool hitTestInteractiveArea(const QPointF& pos) const;
    bool isInteractionActive() const { return m_mode != InteractionMode::None; }

    /// Sync arc rotation from external source (e.g. camera rotation).
    /// Call when rotation changes outside the joystick (e.g. rotate view gesture).
    void setRingRotation(qreal radians);
    qreal ringRotation() const { return m_ringRotation; }

    /// Source coordinating the circular same-frame GPU blur region.
    void setBackdropSource(ruwa::shared::rendering::ICanvasBackdropSource* source);
    QRectF backdropBlurRect() const;

signals:
    /// Normalized pan vector in range [-1..1] for each axis.
    void panRequested(const QPointF& normalizedVector);
    /// Delta rotation in radians for camera/view rotation.
    void rotationRequested(qreal deltaRadians);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    enum class InteractionMode { None, Joystick, RingRotate };

    QPointF centerPoint() const;
    qreal outerRadius() const;
    qreal joystickBaseRadius() const;
    qreal knobRadius() const;
    qreal knobTravelRadius() const;
    qreal ringRadius() const;
    qreal ringThickness() const;
    qreal ringSweepRadians() const;
    bool isInJoystickArea(const QPointF& pos) const;
    bool isInRingArea(const QPointF& pos, qreal extraTolerance = 0.0) const;
    bool isInHoverArea(const QPointF& pos) const;
    qreal pointAngle(const QPointF& pos) const;
    QPointF clampToKnobTravel(const QPointF& offset) const;
    QPointF normalizedKnobOffset() const;
    void startRingHoverAnimation(bool hovered);
    void startKnobReturnAnimation();
    void stopKnobReturnAnimation();
    void updateHoverState(const QPointF& pos);
    void updateCursorForState(const QPointF& pos);
    void resetInteractionState();
    void emitPanStep();

private:
    InteractionMode m_mode = InteractionMode::None;
    QPointF m_knobOffset;
    qreal m_ringRotation = 0.0;
    qreal m_ringHoverProgress = 0.0;
    qreal m_lastRingAngle = 0.0;
    bool m_ringHovered = false;

    QPropertyAnimation* m_ringHoverAnim = nullptr;
    QVariantAnimation* m_knobReturnAnim = nullptr;
    QTimer* m_panTimer = nullptr;

    // Backdrop-blur source (non-owning; nulled on source destruction).
    ruwa::shared::rendering::ICanvasBackdropSource* m_backdropSource = nullptr;

    static constexpr int kWidgetSizePx = 250;
    static constexpr int kPanIntervalMs = 16;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_CANVASSTYLUSJOYSTICKWIDGET_H
