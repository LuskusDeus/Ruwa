// SPDX-License-Identifier: MPL-2.0

#include "CanvasParameterOverlayWidget.h"

#include <QEasingCurve>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace ruwa::ui::workspace {
namespace {

constexpr qreal kHitHalfWidthPx = 7.0;

} // namespace

CanvasParameterOverlayWidget::CanvasParameterOverlayWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);

    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(120);
    connect(
        m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_hoverProgress = value.toReal();
            notifyPresentationChanged();
        });
    connect(m_hoverAnimation, &QVariantAnimation::finished, this, [this]() {
        if (m_hoveredCircle < 0 && m_hoverProgress <= 0.0) {
            m_hoverVisualCircle = -1;
        }
        notifyPresentationChanged();
    });
    hide();
}

void CanvasParameterOverlayWidget::setDocumentToLocalFn(DocumentToLocalFn fn)
{
    m_documentToLocal = std::move(fn);
    notifyPresentationChanged();
}

void CanvasParameterOverlayWidget::setPresentationChangedFn(PresentationChangedFn fn)
{
    m_presentationChanged = std::move(fn);
    notifyPresentationChanged();
}

void CanvasParameterOverlayWidget::setCircles(const QList<CanvasParameterCircleControl>& circles)
{
    m_circles = circles;
    if (m_hoveredCircle >= m_circles.size()) {
        m_hoveredCircle = -1;
    }
    if (m_hoverVisualCircle >= m_circles.size()) {
        m_hoverVisualCircle = -1;
        m_hoverProgress = 0.0;
    }
    setVisible(!m_circles.isEmpty());
    notifyPresentationChanged();
}

const CanvasParameterCircleControl* CanvasParameterOverlayWidget::circleAt(int index) const
{
    return index >= 0 && index < m_circles.size() ? &m_circles.at(index) : nullptr;
}

int CanvasParameterOverlayWidget::circleIndex(const QString& id) const
{
    for (int i = 0; i < m_circles.size(); ++i) {
        if (m_circles.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void CanvasParameterOverlayWidget::setCircleRadius(const QString& id, qreal radius)
{
    const int index = circleIndex(id);
    if (index < 0) {
        return;
    }
    m_circles[index].documentRadius = radius;
    notifyPresentationChanged();
}

CanvasParameterOverlayWidget::ScreenCircle CanvasParameterOverlayWidget::screenCircle(
    const CanvasParameterCircleControl& circle) const
{
    if (!m_documentToLocal) {
        return {};
    }
    const QPointF center = m_documentToLocal(circle.documentCenter);
    const QPointF xEdge
        = m_documentToLocal(circle.documentCenter + QPointF(circle.documentRadius, 0.0));
    const QPointF yEdge
        = m_documentToLocal(circle.documentCenter + QPointF(0.0, circle.documentRadius));
    const qreal xRadius = std::hypot(xEdge.x() - center.x(), xEdge.y() - center.y());
    const qreal yRadius = std::hypot(yEdge.x() - center.x(), yEdge.y() - center.y());
    return { center, (xRadius + yRadius) * 0.5 };
}

int CanvasParameterOverlayWidget::hitTest(const QPointF& localPosition) const
{
    // Last control is visually on top and therefore wins overlapping hits.
    for (int i = m_circles.size() - 1; i >= 0; --i) {
        const ScreenCircle screen = screenCircle(m_circles.at(i));
        const qreal pointerRadius = std::hypot(
            localPosition.x() - screen.center.x(), localPosition.y() - screen.center.y());
        if (std::abs(pointerRadius - screen.radius) <= kHitHalfWidthPx) {
            return i;
        }
    }
    return -1;
}

void CanvasParameterOverlayWidget::setHoveredCircle(int index)
{
    const int resolved = index >= 0 && index < m_circles.size() ? index : -1;
    if (m_hoveredCircle == resolved) {
        return;
    }
    m_hoveredCircle = resolved;
    m_hoverAnimation->stop();
    if (resolved >= 0) {
        if (m_hoverVisualCircle != resolved) {
            m_hoverVisualCircle = resolved;
            m_hoverProgress = 0.0;
        }
        m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_hoverAnimation->setStartValue(m_hoverProgress);
        m_hoverAnimation->setEndValue(1.0);
    } else {
        m_hoverAnimation->setEasingCurve(QEasingCurve::InCubic);
        m_hoverAnimation->setStartValue(m_hoverProgress);
        m_hoverAnimation->setEndValue(0.0);
    }
    m_hoverAnimation->start();
}

CanvasParameterOverlayWidget::ScreenCircle CanvasParameterOverlayWidget::screenCircleAt(
    int index) const
{
    return index >= 0 && index < m_circles.size() ? screenCircle(m_circles.at(index))
                                                  : ScreenCircle {};
}

qreal CanvasParameterOverlayWidget::hoverProgress(int index) const
{
    return index == m_hoverVisualCircle ? m_hoverProgress : 0.0;
}

void CanvasParameterOverlayWidget::notifyPresentationChanged()
{
    if (m_presentationChanged) {
        m_presentationChanged();
    }
}

} // namespace ruwa::ui::workspace
