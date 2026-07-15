// SPDX-License-Identifier: MPL-2.0

// DropZoneIndicator.cpp
#include "DropZoneIndicator.h"

#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QtMath>

namespace ruwa::ui::docking {

namespace {

constexpr int kGlassBlurRadius = 24;
constexpr int kGlassCornerRadius = 8;

QPixmap blurPixmapForGlass(const QPixmap& source, int radius)
{
    if (source.isNull() || radius <= 0) {
        return source;
    }

    const qreal dpr = source.devicePixelRatio();
    const QSize logicalSize(
        qMax(1, qRound(source.width() / dpr)), qMax(1, qRound(source.height() / dpr)));
    constexpr int downsample = 2;
    const QSize smallLogical(
        qMax(1, logicalSize.width() / downsample), qMax(1, logicalSize.height() / downsample));
    const QSize smallDevice(
        qMax(1, qRound(smallLogical.width() * dpr)), qMax(1, qRound(smallLogical.height() * dpr)));

    QImage downscaled = source.toImage()
                            .convertToFormat(QImage::Format_ARGB32_Premultiplied)
                            .scaled(smallDevice, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    downscaled.setDevicePixelRatio(1.0);

    const qreal effectiveRadius = qMax(1.0, qreal(radius) / downsample);
    const int pad = qBound(2, qCeil(effectiveRadius * 2.0), 64);
    const QSize paddedSize(smallDevice.width() + pad * 2, smallDevice.height() + pad * 2);

    QImage padded(paddedSize, QImage::Format_ARGB32_Premultiplied);
    padded.fill(Qt::transparent);
    {
        QPainter p(&padded);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        // Tile the source so edges blur into similar content instead of
        // bleeding to transparent.
        p.drawImage(QRect(QPoint(0, 0), paddedSize), downscaled);
        p.drawImage(QPoint(pad, pad), downscaled);
    }

    QGraphicsScene scene;
    auto* item = new QGraphicsPixmapItem(QPixmap::fromImage(std::move(padded)));
    auto* effect = new QGraphicsBlurEffect;
    effect->setBlurRadius(effectiveRadius);
    effect->setBlurHints(QGraphicsBlurEffect::QualityHint);
    item->setGraphicsEffect(effect);
    scene.addItem(item);

    // Render the blurred scene directly into the final upscaled image, sampling
    // only the unpadded interior. This fuses the previous crop+upscale steps
    // and eliminates one full-frame intermediate allocation per backdrop refresh.
    const QSize finalDevice(qRound(logicalSize.width() * dpr), qRound(logicalSize.height() * dpr));
    QImage upscaled(finalDevice, QImage::Format_ARGB32_Premultiplied);
    upscaled.fill(Qt::transparent);
    {
        QPainter p(&upscaled);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF dstRect(0, 0, finalDevice.width(), finalDevice.height());
        const QRectF srcRect(pad, pad, smallDevice.width(), smallDevice.height());
        scene.render(&p, dstRect, srcRect);
    }

    QPixmap result = QPixmap::fromImage(std::move(upscaled));
    result.setDevicePixelRatio(dpr);
    return result;
}

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
    const QRectF targetLocalF(targetLocal);

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

    m_animation->setDuration(m_animationDuration);
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
    QWidget* window = parent->window();
    if (!window) {
        return;
    }

    // Map the final target rect (in parent overlay coordinates) into window
    // coordinates so we can grab the corresponding window region. The grab
    // captures whatever was painted under the indicator - including the
    // dragged floating panel if it overlaps - which is the desired backdrop.
    const QPoint topLeftInWindow = parent->mapTo(window, m_targetRect.topLeft());
    const QRect grabRect(topLeftInWindow, m_targetRect.size());
    QPixmap snapshot = window->grab(grabRect);
    if (snapshot.isNull()) {
        return;
    }

    m_glassBackdrop = blurPixmapForGlass(snapshot, kGlassBlurRadius);
    m_glassBackdropRect = m_targetRect;
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

    default:
        return m_targetRect;
    }
}

} // namespace ruwa::ui::docking
