// SPDX-License-Identifier: MPL-2.0

// DockCompassWidget.cpp
#include "DockCompassWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEasingCurve>

namespace ruwa::ui::docking {

DockCompassWidget::DockCompassWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);

    // Add padding for antialiasing on edges
    int totalSize = m_size + m_padding * 2;
    setFixedSize(totalSize, totalSize);

    // Default colors
    m_bgNormalColor = QColor(0, 0, 0, 77); // Black with ~30% opacity
    m_bgHoverColor = QColor(60, 60, 60, 200); // Dark gray on hover
    m_arrowColor = QColor(255, 255, 255); // White arrows

    // Initialize zone states and eagerly create the per-zone hover animations
    // up front. Lazy allocation on first hover used to incur a heap alloc and
    // a connect() call on the user's first interaction with each direction.
    constexpr DropZone zones[]
        = { DropZone::InnerTop, DropZone::InnerBottom, DropZone::InnerLeft, DropZone::InnerRight };
    for (DropZone z : zones) {
        ZoneState& state = m_zoneStates[z];
        auto* anim = new QVariantAnimation(this);
        anim->setDuration(m_animationDuration);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this, [this, z](const QVariant& value) {
            auto it = m_zoneStates.find(z);
            if (it != m_zoneStates.end()) {
                it->hoverProgress = value.toReal();
                update();
            }
        });
        state.animation = anim;
    }

    // Setup widget opacity animation
    m_opacityAnimation = new QVariantAnimation(this);
    m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_opacityAnimation, &QVariantAnimation::valueChanged, this,
        &DockCompassWidget::onOpacityAnimationValueChanged);
    connect(m_opacityAnimation, &QVariantAnimation::finished, this,
        &DockCompassWidget::onOpacityAnimationFinished);
}

DockCompassWidget::~DockCompassWidget()
{
    if (m_opacityAnimation) {
        m_opacityAnimation->stop();
    }
    // Zone animations are children, will be deleted automatically
}

// ============================================================================
// Visibility Animation
// ============================================================================

void DockCompassWidget::showAnimated()
{
    m_hiding = false;

    // Fresh show or continue from current opacity
    if (!isVisible()) {
        m_widgetOpacity = 0.0;
        show();
    }

    // startOpacityAnimation handles idempotency (already running toward 1.0,
    // or already at 1.0).
    startOpacityAnimation(1.0);
}

void DockCompassWidget::hideAnimated()
{
    // Already hidden
    if (!isVisible()) {
        return;
    }
    // Already hiding to 0 - leave the in-flight animation alone.
    if (m_hiding) {
        return;
    }

    m_hiding = true;
    startOpacityAnimation(0.0);
}

void DockCompassWidget::hideImmediate()
{
    if (m_opacityAnimation && m_opacityAnimation->state() == QAbstractAnimation::Running) {
        m_opacityAnimation->stop();
    }

    m_widgetOpacity = 0.0;
    m_hiding = false;
    setHighlightedZone(DropZone::None);
    hide();
}

bool DockCompassWidget::isActiveOrShowing() const
{
    // Active if visible, not hiding, and has some opacity (or animation running towards 1.0)
    if (!isVisible()) {
        return false;
    }
    if (m_hiding) {
        return false;
    }
    // Consider active if has opacity or animation is running (towards showing)
    if (m_widgetOpacity > 0.0) {
        return true;
    }
    // Also active if animation is running towards 1.0
    if (m_opacityAnimation && m_opacityAnimation->state() == QAbstractAnimation::Running) {
        return m_opacityAnimation->endValue().toReal() > 0.0;
    }
    return false;
}

// ============================================================================
// State
// ============================================================================

void DockCompassWidget::setHighlightedZone(DropZone zone)
{
    if (m_highlightedZone != zone) {
        DropZone oldZone = m_highlightedZone;
        m_highlightedZone = zone;

        // Animate out old zone
        if (oldZone != DropZone::None) {
            startHoverAnimation(oldZone, false);
        }

        // Animate in new zone
        if (zone != DropZone::None) {
            startHoverAnimation(zone, true);
        }

        emit zoneHovered(zone);
    }
}

DropZone DockCompassWidget::zoneAt(const QPoint& localPos) const
{
    // Check each zone
    if (zoneRect(DropZone::InnerTop).contains(localPos))
        return DropZone::InnerTop;
    if (zoneRect(DropZone::InnerBottom).contains(localPos))
        return DropZone::InnerBottom;
    if (zoneRect(DropZone::InnerLeft).contains(localPos))
        return DropZone::InnerLeft;
    if (zoneRect(DropZone::InnerRight).contains(localPos))
        return DropZone::InnerRight;

    return DropZone::None;
}

bool DockCompassWidget::containsPoint(const QPoint& localPos) const
{
    return zoneAt(localPos) != DropZone::None;
}

// ============================================================================
// Appearance
// ============================================================================

void DockCompassWidget::setCompassSize(int size)
{
    m_size = size;
    int totalSize = m_size + m_padding * 2;
    setFixedSize(totalSize, totalSize);
    update();
}

void DockCompassWidget::setZoneSize(int size)
{
    m_zoneSize = size;
    update();
}

void DockCompassWidget::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    // Background stays black with transparency
    m_bgNormalColor = QColor(0, 0, 0, 77); // ~30% opacity
    m_bgHoverColor = QColor(60, 60, 60, 200); // Dark gray on hover

    // Arrow color from theme text
    m_arrowColor = colors.text;

    update();
}

// ============================================================================
// Events
// ============================================================================

void DockCompassWidget::paintEvent(QPaintEvent* /*event*/)
{
    // Skip painting if fully transparent
    if (m_widgetOpacity <= 0.0) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Apply widget-level opacity
    painter.setOpacity(m_widgetOpacity);

    // Draw each zone
    drawZone(painter, DropZone::InnerTop);
    drawZone(painter, DropZone::InnerBottom);
    drawZone(painter, DropZone::InnerLeft);
    drawZone(painter, DropZone::InnerRight);
}

void DockCompassWidget::mouseMoveEvent(QMouseEvent* event)
{
    setHighlightedZone(zoneAt(event->pos()));
    QWidget::mouseMoveEvent(event);
}

void DockCompassWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        DropZone zone = zoneAt(event->pos());
        if (zone != DropZone::None) {
            emit zoneClicked(zone);
        }
    }
    QWidget::mousePressEvent(event);
}

void DockCompassWidget::leaveEvent(QEvent* event)
{
    setHighlightedZone(DropZone::None);
    QWidget::leaveEvent(event);
}

// ============================================================================
// Private
// ============================================================================

QRect DockCompassWidget::zoneRect(DropZone zone) const
{
    // Center is offset by padding
    int centerX = m_padding + m_size / 2;
    int centerY = m_padding + m_size / 2;
    int halfZone = m_zoneSize / 2;
    int offset = halfZone + m_spacing;

    switch (zone) {
    case DropZone::InnerTop:
        return QRect(centerX - halfZone, centerY - offset - m_zoneSize, m_zoneSize, m_zoneSize);

    case DropZone::InnerBottom:
        return QRect(centerX - halfZone, centerY + offset, m_zoneSize, m_zoneSize);

    case DropZone::InnerLeft:
        return QRect(centerX - offset - m_zoneSize, centerY - halfZone, m_zoneSize, m_zoneSize);

    case DropZone::InnerRight:
        return QRect(centerX + offset, centerY - halfZone, m_zoneSize, m_zoneSize);

    default:
        return QRect();
    }
}

void DockCompassWidget::drawZone(QPainter& painter, DropZone zone)
{
    QRect r = zoneRect(zone);
    if (r.isEmpty())
        return;

    // Get animation progress for this zone
    qreal progress = 0.0;
    if (m_zoneStates.contains(zone)) {
        progress = m_zoneStates[zone].hoverProgress;
    }

    // Interpolate background color
    QColor bgColor;
    bgColor.setRed(
        m_bgNormalColor.red() + (m_bgHoverColor.red() - m_bgNormalColor.red()) * progress);
    bgColor.setGreen(
        m_bgNormalColor.green() + (m_bgHoverColor.green() - m_bgNormalColor.green()) * progress);
    bgColor.setBlue(
        m_bgNormalColor.blue() + (m_bgHoverColor.blue() - m_bgNormalColor.blue()) * progress);
    bgColor.setAlpha(
        m_bgNormalColor.alpha() + (m_bgHoverColor.alpha() - m_bgNormalColor.alpha()) * progress);

    // Draw background (no border)
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(r, m_borderRadius, m_borderRadius);

    // Calculate arrow scale (1.0 -> 1.2 on hover)
    qreal arrowScale = 1.0 + (0.2 * progress);

    // Draw arrow
    drawArrow(painter, r, zone, arrowScale);
}

void DockCompassWidget::drawArrow(QPainter& painter, const QRect& rect, DropZone zone, qreal scale)
{
    painter.save();

    // Set arrow color
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_arrowColor);

    // Arrow size (base)
    qreal baseSize = qMin(rect.width(), rect.height()) * 0.35;
    qreal size = baseSize * scale;

    // Center of rect
    QPointF center = rect.center();

    // Create arrow path based on direction
    QPainterPath path;

    switch (zone) {
    case DropZone::InnerTop: {
        // Arrow pointing up (triangle)
        path.moveTo(center.x(), center.y() - size * 0.6);
        path.lineTo(center.x() - size * 0.5, center.y() + size * 0.4);
        path.lineTo(center.x() + size * 0.5, center.y() + size * 0.4);
        path.closeSubpath();
        break;
    }
    case DropZone::InnerBottom: {
        // Arrow pointing down
        path.moveTo(center.x(), center.y() + size * 0.6);
        path.lineTo(center.x() - size * 0.5, center.y() - size * 0.4);
        path.lineTo(center.x() + size * 0.5, center.y() - size * 0.4);
        path.closeSubpath();
        break;
    }
    case DropZone::InnerLeft: {
        // Arrow pointing left
        path.moveTo(center.x() - size * 0.6, center.y());
        path.lineTo(center.x() + size * 0.4, center.y() - size * 0.5);
        path.lineTo(center.x() + size * 0.4, center.y() + size * 0.5);
        path.closeSubpath();
        break;
    }
    case DropZone::InnerRight: {
        // Arrow pointing right
        path.moveTo(center.x() + size * 0.6, center.y());
        path.lineTo(center.x() - size * 0.4, center.y() - size * 0.5);
        path.lineTo(center.x() - size * 0.4, center.y() + size * 0.5);
        path.closeSubpath();
        break;
    }
    default:
        break;
    }

    painter.drawPath(path);
    painter.restore();
}

void DockCompassWidget::ensureAnimation(DropZone /*zone*/)
{
    // All zone animations are pre-allocated in the constructor. Kept as a
    // no-op for compatibility with the existing call site.
}

void DockCompassWidget::startHoverAnimation(DropZone zone, bool hovering)
{
    auto it = m_zoneStates.find(zone);
    if (it == m_zoneStates.end() || !it->animation) {
        return;
    }
    QVariantAnimation* anim = it->animation;

    const qreal target = hovering ? 1.0 : 0.0;

    // Idempotent: don't restart an animation that's already heading to the
    // same target — restarting from the current progress over the full
    // duration produces an asymptotic "never quite arrives" feel.
    if (anim->state() == QAbstractAnimation::Running
        && qFuzzyCompare(anim->endValue().toReal() + 1.0, target + 1.0)) {
        return;
    }

    if (anim->state() == QAbstractAnimation::Running) {
        anim->stop();
    }

    qreal currentProgress = it->hoverProgress;
    anim->setStartValue(currentProgress);
    anim->setEndValue(target);
    anim->setDuration(m_animationDuration);
    anim->start();
}

// ============================================================================
// Opacity Animation Slots
// ============================================================================

void DockCompassWidget::onOpacityAnimationValueChanged(const QVariant& value)
{
    m_widgetOpacity = value.toReal();
    update();
}

void DockCompassWidget::onOpacityAnimationFinished()
{
    if (m_hiding) {
        m_hiding = false;
        setHighlightedZone(DropZone::None);
        hide();
    }
}

void DockCompassWidget::startOpacityAnimation(qreal targetOpacity)
{
    if (!m_opacityAnimation) {
        return;
    }

    // Idempotency: if we're already animating toward the same target, leave it
    // alone. Without this, repeated calls (e.g. from the 60Hz geometry-update
    // timer) restart the animation every frame from the current opacity to the
    // target over the full duration, which makes the fade-in asymptotically
    // slow and visibly "stutter" or "freeze".
    if (m_opacityAnimation->state() == QAbstractAnimation::Running
        && qFuzzyCompare(m_opacityAnimation->endValue().toReal() + 1.0, targetOpacity + 1.0)) {
        return;
    }

    // Already at target with no animation in flight - nothing to do.
    if (m_opacityAnimation->state() != QAbstractAnimation::Running
        && qFuzzyCompare(m_widgetOpacity + 1.0, targetOpacity + 1.0)) {
        return;
    }

    if (m_opacityAnimation->state() == QAbstractAnimation::Running) {
        m_opacityAnimation->stop();
    }

    // Skip animation if duration is 0
    if (m_opacityAnimationDuration <= 0) {
        m_widgetOpacity = targetOpacity;
        if (targetOpacity <= 0.0 && m_hiding) {
            m_hiding = false;
            setHighlightedZone(DropZone::None);
            hide();
        }
        update();
        return;
    }

    m_opacityAnimation->setStartValue(m_widgetOpacity);
    m_opacityAnimation->setEndValue(targetOpacity);
    m_opacityAnimation->setDuration(m_opacityAnimationDuration);
    m_opacityAnimation->start();
}

} // namespace ruwa::ui::docking
