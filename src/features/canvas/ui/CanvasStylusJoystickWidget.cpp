// SPDX-License-Identifier: MPL-2.0

#include "CanvasStylusJoystickWidget.h"

#include "shared/style/WidgetStyleManager.h"
#include "shared/style/PaintingUtils.h"
#include "features/theme/manager/ThemeManager.h"

#include <QEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QTabletEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QtMath>

namespace ruwa::ui::widgets {

namespace {

constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kStylusHitPadding = 14.0;

qreal normalizeAngleDelta(qreal delta)
{
    while (delta > kPi) {
        delta -= 2.0 * kPi;
    }
    while (delta < -kPi) {
        delta += 2.0 * kPi;
    }
    return delta;
}

} // namespace

CanvasStylusJoystickWidget::CanvasStylusJoystickWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setTabletTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(kWidgetSizePx, kWidgetSizePx);

    m_ringHoverAnim = new QPropertyAnimation(this, "ringHoverProgress", this);
    m_ringHoverAnim->setDuration(180);
    m_ringHoverAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_knobReturnAnim = new QVariantAnimation(this);
    m_knobReturnAnim->setDuration(200);
    m_knobReturnAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(
        m_knobReturnAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            if (!value.canConvert<QPointF>()) {
                return;
            }
            m_knobOffset = value.toPointF();
            update();
        });

    m_panTimer = new QTimer(this);
    m_panTimer->setInterval(kPanIntervalMs);
    connect(m_panTimer, &QTimer::timeout, this, [this]() { emitPanStep(); });
}

CanvasStylusJoystickWidget::~CanvasStylusJoystickWidget() = default;

void CanvasStylusJoystickWidget::setRingHoverProgress(qreal progress)
{
    const qreal clamped = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_ringHoverProgress, clamped)) {
        return;
    }
    m_ringHoverProgress = clamped;
    update();
}

void CanvasStylusJoystickWidget::setRingRotation(qreal radians)
{
    if (qFuzzyCompare(m_ringRotation, radians)) {
        return;
    }
    m_ringRotation = radians;
    update();
}

void CanvasStylusJoystickWidget::setBackdropSource(
    ruwa::shared::rendering::ICanvasBackdropSource* source)
{
    if (m_backdropSource == source) {
        return;
    }
    m_backdropSource = source;
    update();
}

QRectF CanvasStylusJoystickWidget::backdropBlurRect() const
{
    const QPointF center = centerPoint();
    const qreal radius = joystickBaseRadius();
    return QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
}

void CanvasStylusJoystickWidget::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    // Keep the GPU blur region synchronized with the panel-swap animation.
    if (m_backdropSource) {
        m_backdropSource->requestBackdropUpdate();
        update();
    }
}

QPointF CanvasStylusJoystickWidget::centerPoint() const
{
    return rect().center();
}

qreal CanvasStylusJoystickWidget::outerRadius() const
{
    return qMin(width(), height()) * 0.5 - 4.0;
}

qreal CanvasStylusJoystickWidget::joystickBaseRadius() const
{
    return outerRadius() * 0.58;
}

qreal CanvasStylusJoystickWidget::knobRadius() const
{
    return joystickBaseRadius() * 0.34;
}

qreal CanvasStylusJoystickWidget::knobTravelRadius() const
{
    return joystickBaseRadius() - knobRadius() - 6.0;
}

qreal CanvasStylusJoystickWidget::ringRadius() const
{
    return joystickBaseRadius() + 22.0 + 3.0 * m_ringHoverProgress;
}

qreal CanvasStylusJoystickWidget::ringThickness() const
{
    return 7.0 + 1.2 * m_ringHoverProgress;
}

qreal CanvasStylusJoystickWidget::ringSweepRadians() const
{
    // Base top arc that gently expands on hover.
    return qDegreesToRadians(110.0 + 12.0 * m_ringHoverProgress);
}

bool CanvasStylusJoystickWidget::isInJoystickArea(const QPointF& pos) const
{
    const QPointF v = pos - centerPoint();
    const qreal dist = std::hypot(v.x(), v.y());
    return dist <= joystickBaseRadius() + kStylusHitPadding;
}

bool CanvasStylusJoystickWidget::isInRingArea(const QPointF& pos, qreal extraTolerance) const
{
    const QPointF c = centerPoint();
    const QPointF v = pos - c;
    const qreal dist = std::hypot(v.x(), v.y());
    const qreal halfThickness = ringThickness() * 0.5 + extraTolerance;
    const qreal minDist = ringRadius() - halfThickness;
    const qreal maxDist = ringRadius() + halfThickness;
    if (dist < minDist || dist > maxDist) {
        return false;
    }

    const qreal angle = std::atan2(v.y(), v.x());
    const qreal sweep = ringSweepRadians();
    const qreal center = -kPi / 2.0 + m_ringRotation;
    const qreal half = sweep * 0.5;
    const qreal delta = normalizeAngleDelta(angle - center);
    return std::abs(delta) <= half;
}

bool CanvasStylusJoystickWidget::isInHoverArea(const QPointF& pos) const
{
    return isInRingArea(pos, kStylusHitPadding * 0.35) || isInJoystickArea(pos);
}

bool CanvasStylusJoystickWidget::hitTestInteractiveArea(const QPointF& pos) const
{
    return isInJoystickArea(pos) || isInRingArea(pos, kStylusHitPadding);
}

qreal CanvasStylusJoystickWidget::pointAngle(const QPointF& pos) const
{
    const QPointF v = pos - centerPoint();
    return std::atan2(v.y(), v.x());
}

QPointF CanvasStylusJoystickWidget::clampToKnobTravel(const QPointF& offset) const
{
    const qreal dist = std::hypot(offset.x(), offset.y());
    const qreal maxDist = knobTravelRadius();
    if (dist <= maxDist || qFuzzyIsNull(dist)) {
        return offset;
    }
    const qreal scale = maxDist / dist;
    return QPointF(offset.x() * scale, offset.y() * scale);
}

QPointF CanvasStylusJoystickWidget::normalizedKnobOffset() const
{
    const qreal travel = qMax(1.0, knobTravelRadius());
    const QPointF normalized(m_knobOffset.x() / travel, m_knobOffset.y() / travel);
    return QPointF(qBound(-1.0, normalized.x(), 1.0), qBound(-1.0, normalized.y(), 1.0));
}

void CanvasStylusJoystickWidget::startRingHoverAnimation(bool hovered)
{
    const qreal target = hovered ? 1.0 : 0.0;
    if (qFuzzyCompare(m_ringHoverProgress, target)) {
        return;
    }
    m_ringHoverAnim->stop();
    m_ringHoverAnim->setStartValue(m_ringHoverProgress);
    m_ringHoverAnim->setEndValue(target);
    m_ringHoverAnim->start();
}

void CanvasStylusJoystickWidget::startKnobReturnAnimation()
{
    m_knobReturnAnim->stop();
    m_knobReturnAnim->setStartValue(m_knobOffset);
    m_knobReturnAnim->setEndValue(QPointF(0.0, 0.0));
    m_knobReturnAnim->start();
}

void CanvasStylusJoystickWidget::stopKnobReturnAnimation()
{
    if (m_knobReturnAnim->state() == QAbstractAnimation::Running) {
        m_knobReturnAnim->stop();
    }
}

void CanvasStylusJoystickWidget::updateHoverState(const QPointF& pos)
{
    const bool hovered = isInHoverArea(pos);
    if (hovered == m_ringHovered) {
        return;
    }
    m_ringHovered = hovered;
    startRingHoverAnimation(hovered || m_mode == InteractionMode::RingRotate);
}

void CanvasStylusJoystickWidget::updateCursorForState(const QPointF& pos)
{
    if (m_mode != InteractionMode::None) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (isInHoverArea(pos)) {
        setCursor(Qt::OpenHandCursor);
    } else {
        unsetCursor();
    }
}

void CanvasStylusJoystickWidget::resetInteractionState()
{
    const InteractionMode previousMode = m_mode;
    m_mode = InteractionMode::None;

    if (previousMode == InteractionMode::Joystick) {
        m_panTimer->stop();
        emit panRequested(QPointF(0.0, 0.0));
        startKnobReturnAnimation();
    }

    m_ringHovered = false;
    startRingHoverAnimation(false);
    unsetCursor();
}

void CanvasStylusJoystickWidget::emitPanStep()
{
    emit panRequested(normalizedKnobOffset());
}

void CanvasStylusJoystickWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    auto& colors = ruwa::ui::core::WidgetStyleManager::instance().colors();

    const QPointF c = centerPoint();
    const qreal baseR = joystickBaseRadius();
    const qreal stickR = knobRadius();
    const QPointF stickCenter = c + m_knobOffset;

    // Base body: same-frame GPU backdrop blur clipped to the circular base.
    painter.setPen(Qt::NoPen);
    QPainterPath basePath;
    basePath.addEllipse(c, baseR, baseR);
    QColor baseTint = colors.surface;
    baseTint.setAlpha(ruwa::ui::painting::kBackdropTintAlpha);
    if (!ruwa::ui::painting::drawBackdropBlurTint(
            painter, this, m_backdropSource, basePath, baseTint)) {
        QColor baseBg = colors.surface;
        baseBg.setAlpha(200);
        painter.setBrush(baseBg);
        painter.drawEllipse(c, baseR, baseR);
    }

    const QRectF bodyRect(
        c.x() - baseR + 0.5, c.y() - baseR + 0.5, baseR * 2.0 - 1.0, baseR * 2.0 - 1.0);
    QPen bodyPen(colors.border);
    bodyPen.setWidthF(1.0);
    bodyPen.setCosmetic(true);
    painter.setPen(bodyPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(bodyRect);

    // Upper rotating arc with rounded caps.
    const qreal sweep = ringSweepRadians();
    const qreal arcCenter = -kPi / 2.0 + m_ringRotation;
    const qreal start = arcCenter - sweep * 0.5;
    const qreal end = arcCenter + sweep * 0.5;
    const qreal rr = ringRadius();
    const qreal thickness = ringThickness();

    QPainterPath ringPath;
    ringPath.moveTo(c + QPointF(std::cos(start) * rr, std::sin(start) * rr));
    const int segments = 36;
    for (int i = 1; i <= segments; ++i) {
        const qreal t = static_cast<qreal>(i) / static_cast<qreal>(segments);
        const qreal a = start + (end - start) * t;
        ringPath.lineTo(c + QPointF(std::cos(a) * rr, std::sin(a) * rr));
    }

    QColor ringColor = ruwa::ui::core::ThemeColors::interpolate(
        colors.textMuted, colors.text, m_ringHoverProgress * 0.3);
    ringColor.setAlpha(190);
    QPen ringPen(ringColor, thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(ringPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(ringPath);

    QColor stickColor = ringColor;
    stickColor.setAlpha(205);
    painter.setPen(Qt::NoPen);
    painter.setBrush(stickColor);
    painter.drawEllipse(stickCenter, stickR, stickR);
}

void CanvasStylusJoystickWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPointF pos = event->position();
    updateHoverState(pos);

    if (isInRingArea(pos, kStylusHitPadding)) {
        m_mode = InteractionMode::RingRotate;
        m_lastRingAngle = pointAngle(pos);
        startRingHoverAnimation(true);
        updateCursorForState(pos);
        event->accept();
        return;
    }

    if (isInJoystickArea(pos)) {
        m_mode = InteractionMode::Joystick;
        stopKnobReturnAnimation();
        m_knobOffset = clampToKnobTravel(pos - centerPoint());
        if (!m_panTimer->isActive()) {
            m_panTimer->start();
        }
        updateCursorForState(pos);
        emitPanStep();
        update();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void CanvasStylusJoystickWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    updateHoverState(pos);
    updateCursorForState(pos);

    if (m_mode == InteractionMode::RingRotate) {
        const qreal angle = pointAngle(pos);
        const qreal delta = normalizeAngleDelta(angle - m_lastRingAngle);
        m_lastRingAngle = angle;
        m_ringRotation += delta;
        emit rotationRequested(delta);
        update();
        event->accept();
        return;
    }

    if (m_mode == InteractionMode::Joystick) {
        m_knobOffset = clampToKnobTravel(pos - centerPoint());
        // Pan step is emitted by the fixed-rate timer, so pointer device event
        // frequency (mouse vs stylus) does not affect movement speed.
        update();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void CanvasStylusJoystickWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_mode != InteractionMode::None) {
        const InteractionMode released = m_mode;
        m_mode = InteractionMode::None;

        if (released == InteractionMode::Joystick) {
            m_panTimer->stop();
            emit panRequested(QPointF(0.0, 0.0));
            startKnobReturnAnimation();
        } else if (released == InteractionMode::RingRotate) {
            updateHoverState(event->position());
            if (!m_ringHovered) {
                startRingHoverAnimation(false);
            }
        }
        updateCursorForState(event->position());

        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void CanvasStylusJoystickWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    resetInteractionState();
}

bool CanvasStylusJoystickWidget::event(QEvent* event)
{
    if (event->type() == QEvent::TabletLeaveProximity || event->type() == QEvent::Hide
        || event->type() == QEvent::WindowDeactivate) {
        resetInteractionState();
    }
    return QWidget::event(event);
}

void CanvasStylusJoystickWidget::tabletEvent(QTabletEvent* event)
{
    // Stylus support: map tablet input to the same high-level interaction model.
    const QPointF localPos = QPointF(mapFromGlobal(event->globalPosition().toPoint()));
    switch (event->type()) {
    case QEvent::TabletPress: {
        if (event->button() == Qt::LeftButton) {
            QMouseEvent syntheticPress(QEvent::MouseButtonPress, localPos, event->globalPosition(),
                Qt::LeftButton, event->buttons(), event->modifiers());
            mousePressEvent(&syntheticPress);
            updateCursorForState(localPos);
            event->accept();
            return;
        }
        break;
    }
    case QEvent::TabletMove: {
        QMouseEvent syntheticMove(QEvent::MouseMove, localPos, event->globalPosition(),
            Qt::NoButton, event->buttons(), event->modifiers());
        mouseMoveEvent(&syntheticMove);
        updateCursorForState(localPos);
        event->accept();
        return;
    }
    case QEvent::TabletRelease: {
        QMouseEvent syntheticRelease(QEvent::MouseButtonRelease, localPos, event->globalPosition(),
            Qt::LeftButton, event->buttons(), event->modifiers());
        mouseReleaseEvent(&syntheticRelease);
        updateCursorForState(localPos);
        event->accept();
        return;
    }
    default:
        break;
    }

    event->ignore();
}

} // namespace ruwa::ui::widgets
