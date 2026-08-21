// SPDX-License-Identifier: MPL-2.0

// DropZoneIndicator.cpp
#include "DropZoneIndicator.h"
#include "shared/style/AnimationPolicy.h"
#include "shared/style/GlassPanel.h"

#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QtMath>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::docking {

namespace {

/// Only reached when the GPU glass is unavailable.
constexpr int kFallbackBlurRadius = 24;
constexpr int kGlassCornerRadius = 8;

/// Group previews grow from this fraction of the cell instead of wiping in from
/// an edge — there is no edge to come from when the drop lands on top.
constexpr qreal kGroupPreviewMinScale = 0.8;

} // namespace

DropZoneIndicator::DropZoneIndicator(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setVisible(false);

    // Default fill color (no border)
    m_fillColor = QColor(100, 150, 255, 60);

    // Setup animation
    m_animation = new QVariantAnimation(this);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_animation, &QVariantAnimation::valueChanged, this,
        &DropZoneIndicator::onAnimationValueChanged);
    connect(
        m_animation, &QVariantAnimation::finished, this, &DropZoneIndicator::onAnimationFinished);
}

DropZoneIndicator::~DropZoneIndicator()
{
    if (m_animation) {
        m_animation->stop();
    }
}

// ============================================================================
// State
// ============================================================================

void DropZoneIndicator::showForZone(const QRect& targetRect, DropZone zone)
{
    const bool targetChanged = (m_targetRect != targetRect);
    m_targetRect = targetRect;

    // (Re)capture the glass backdrop whenever the final target rect changes,
    // or when there isn't one yet. During the slide-in animation itself the
    // backdrop is not refreshed - the widget geometry simply clips it.
    if (targetChanged || m_glassBackdrop.isNull() || m_glassBackdropRect != m_targetRect) {
        captureGlassBackdrop();
    }

    // If same zone and visible - just update geometry
    if (zone == m_zone && isVisible()) {
        // If was hiding, reverse to showing
        if (m_hiding) {
            m_hiding = false;
            if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
                m_animation->stop();
            }
            // Continue from current progress
            startAnimation(true);
        } else {
            // Just update geometry with current progress
            setGeometry(calculateAnimatedRect(m_animationProgress));
            update();
        }
        return;
    }

    // Zone changed - start fresh animation
    if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
    }

    m_zone = zone;
    m_hiding = false;
    m_animationProgress = 0.0;

    setGeometry(calculateAnimatedRect(0.0));
    show();

    startAnimation(true);
}

void DropZoneIndicator::hideIndicator()
{
    if (!isVisible() || m_zone == DropZone::None) {
        return;
    }

    m_hiding = true;
    startAnimation(false);
}

void DropZoneIndicator::hideImmediate()
{
    if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
    }

    m_zone = DropZone::None;
    m_targetRect = QRect();
    m_animationProgress = 0.0;
    m_hiding = false;
    m_glassBackdrop = {};
    m_glassBackdropRect = QRect();
    hide();
}

// ============================================================================
// Appearance
// ============================================================================

void DropZoneIndicator::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    // Use primary color with transparency (no border)
    m_fillColor = colors.primary;
    m_fillColor.setAlpha(50);

    update();
}

// ============================================================================
// Events
// ============================================================================

void DropZoneIndicator::paintEvent(QPaintEvent* /*event*/)
{
    if (m_animationProgress <= 0.0 || m_targetRect.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Express the final target rect in widget-local coordinates. Because the
    // widget geometry follows the slide animation while m_targetRect stays
    // fixed, this offset is non-zero for slide-from-right/bottom zones.
    const QRect widgetGeom = geometry();
    const QPoint offset(m_targetRect.x() - widgetGeom.x(), m_targetRect.y() - widgetGeom.y());
    const QRect targetLocal(offset, m_targetRect.size());

    // An edge zone is *revealed*: the shape stays put and the widget bounds act
    // as a blind. A group zone is *scaled*: the whole shape grows and fades in,
    // so it fills the widget rect and carries the animation's opacity.
    const bool groupPreview = isGroupZone(m_zone);
    const QRectF targetLocalF
        = groupPreview ? QRectF(0, 0, width(), height()) : QRectF(targetLocal);

    if (groupPreview) {
        painter.setOpacity(qBound(0.0, m_animationProgress, 1.0));
    }

    // Clip everything (backdrop + tint) to the rounded final shape. The widget
    // bounds themselves provide the "blind" that exposes only the animated
    // portion.
    QPainterPath clipPath;
    clipPath.addRoundedRect(targetLocalF, kGlassCornerRadius, kGlassCornerRadius);

    painter.save();
    painter.setClipPath(clipPath);

    // 1. Blurred backdrop anchored at the final target position.
    if (!m_glassBackdrop.isNull()) {
        const qreal dpr = m_glassBackdrop.devicePixelRatio();
        const QRectF src(0, 0, m_glassBackdrop.width() / dpr, m_glassBackdrop.height() / dpr);
        painter.drawPixmap(targetLocalF, m_glassBackdrop, src);
    } else {
        painter.fillRect(targetLocalF, QColor(40, 40, 40, 160));
    }

    // 2. Accent tint - same hue as the original indicator, but a touch more
    //    saturated so the glass reads as "drop zone".
    QColor tint = m_fillColor;
    tint.setAlpha(qMin(255, tint.alpha() * 2 + 40));
    painter.fillRect(targetLocalF, tint);

    painter.restore();

    // 3. Subtle border around the final shape. Outside the widget bounds the
    //    border is clipped automatically, which is exactly the "trimmed by the
    //    plate" effect we want during the slide animation.
    QColor borderColor = m_fillColor;
    borderColor.setAlpha(200);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(borderColor, 1.0));
    painter.drawRoundedRect(
        targetLocalF.adjusted(0.5, 0.5, -0.5, -0.5), kGlassCornerRadius, kGlassCornerRadius);
}

// ============================================================================
// Private Slots
// ============================================================================

void DropZoneIndicator::onAnimationValueChanged(const QVariant& value)
{
    m_animationProgress = value.toReal();
    setGeometry(calculateAnimatedRect(m_animationProgress));
    update();
}

void DropZoneIndicator::onAnimationFinished()
{
    if (m_hiding) {
        m_hiding = false;
        m_zone = DropZone::None;
        m_animationProgress = 0.0;
        hide();
    }
}

// ============================================================================
// Private
// ============================================================================

void DropZoneIndicator::startAnimation(bool showing)
{
    if (!m_animation) {
        return;
    }

    if (m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
    }

    if (showing) {
        m_animation->setStartValue(m_animationProgress);
        m_animation->setEndValue(1.0);
    } else {
        m_animation->setStartValue(m_animationProgress);
        m_animation->setEndValue(0.0);
    }

    m_animation->setDuration(anim::duration(m_animationDuration));
    m_animation->start();
}

void DropZoneIndicator::captureGlassBackdrop()
{
    m_glassBackdrop = {};
    m_glassBackdropRect = QRect();

    if (m_targetRect.isEmpty()) {
        return;
    }

    QWidget* parent = parentWidget();
    if (!parent) {
        return;
    }
    // The capture is taken from the window the indicator lives in, so it holds
    // whatever was painted under it - including the dragged floating panel if
    // it overlaps - which is the desired backdrop.
    ruwa::ui::painting::GlassPanelOptics optics;
    // No frost pull: the accent tint painted over this plate is heavy enough
    // that a second one towards the theme surface would only mute it.
    optics.fallbackBlurRadius = kFallbackBlurRadius;
    m_glassBackdrop = ruwa::ui::painting::captureGlassBackdrop(parent,
        QRect(parent->mapToGlobal(m_targetRect.topLeft()), m_targetRect.size()), kGlassCornerRadius,
        optics);
    if (!m_glassBackdrop.isNull()) {
        m_glassBackdropRect = m_targetRect;
    }
}

QRect DropZoneIndicator::calculateAnimatedRect(qreal progress) const
{
    if (m_targetRect.isEmpty()) {
        return QRect();
    }

    int x = m_targetRect.x();
    int y = m_targetRect.y();
    int w = m_targetRect.width();
    int h = m_targetRect.height();

    // Animate from edge - slide out from the corresponding side
    switch (m_zone) {
    case DropZone::OuterLeft:
    case DropZone::InnerLeft:
        // Slide from left edge to the right
        return QRect(x, y, qRound(w * progress), h);

    case DropZone::OuterRight:
    case DropZone::InnerRight:
        // Slide from right edge to the left
        {
            int animW = qRound(w * progress);
            return QRect(x + w - animW, y, animW, h);
        }

    case DropZone::OuterTop:
    case DropZone::InnerTop:
        // Slide from top edge downward
        return QRect(x, y, w, qRound(h * progress));

    case DropZone::OuterBottom:
    case DropZone::InnerBottom:
        // Slide from bottom edge upward
        {
            int animH = qRound(h * progress);
            return QRect(x, y + h - animH, w, animH);
        }

    case DropZone::InnerCenter:
    case DropZone::GroupHeader:
        // Grow about the centre; paintEvent fades it in over the same progress.
        {
            const qreal scale = kGroupPreviewMinScale
                + (1.0 - kGroupPreviewMinScale) * qBound(0.0, progress, 1.0);
            const int sw = qRound(w * scale);
            const int sh = qRound(h * scale);
            return QRect(x + (w - sw) / 2, y + (h - sh) / 2, sw, sh);
        }

    default:
        return m_targetRect;
    }
}

} // namespace ruwa::ui::docking
