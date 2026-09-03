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
        if (m_hoveredControl < 0 && m_hoverProgress <= 0.0) {
            m_hoverVisualControl = -1;
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

void CanvasParameterOverlayWidget::setControls(const QList<CanvasParameterControl>& controls)
{
    const auto* hovered = controlAt(m_hoveredControl);
    const QString hoveredId = hovered ? hovered->id : QString();
    const auto* visual = controlAt(m_hoverVisualControl);
    const QString visualId = visual ? visual->id : QString();
    m_controls = controls;
    m_hoveredControl = controlIndex(hoveredId);
    m_hoverVisualControl = controlIndex(visualId);
    if (m_hoverVisualControl < 0) {
        m_hoverAnimation->stop();
        m_hoverProgress = 0.0;
    }
    setVisible(!m_controls.isEmpty());
    notifyPresentationChanged();
}

const CanvasParameterControl* CanvasParameterOverlayWidget::controlAt(int index) const
{
    return index >= 0 && index < m_controls.size() ? &m_controls.at(index) : nullptr;
}

int CanvasParameterOverlayWidget::controlIndex(const QString& id) const
{
    for (int i = 0; i < m_controls.size(); ++i) {
        if (m_controls.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void CanvasParameterOverlayWidget::setCircleRadius(const QString& id, qreal radius)
{
    const int index = controlIndex(id);
    if (index < 0) {
        return;
    }
    m_controls[index].documentRadius = radius;
    notifyPresentationChanged();
}

void CanvasParameterOverlayWidget::setControlPosition(const QString& id, const QPointF& position)
{
    const auto* control = controlAt(controlIndex(id));
    if (!control) {
        return;
    }
    const QString xKey = control->centerXParamKey;
    const QString yKey = control->centerYParamKey;
    // Keep other controls bound to the same center (e.g. a radius ring) in sync.
    for (auto& item : m_controls) {
        if (item.centerXParamKey == xKey) {
            item.documentCenter.setX(position.x());
        }
        if (item.centerYParamKey == yKey) {
            item.documentCenter.setY(position.y());
        }
    }
    notifyPresentationChanged();
}

CanvasParameterOverlayWidget::ScreenControl CanvasParameterOverlayWidget::screenControl(
    const CanvasParameterControl& control) const
{
    if (!m_documentToLocal) {
        return {};
    }
    const QPointF center = m_documentToLocal(control.documentCenter);
    if (control.type == CanvasParameterControlType::Position) {
        return { center, 0.0 };
    }
    const QPointF xEdge
        = m_documentToLocal(control.documentCenter + QPointF(control.documentRadius, 0.0));
    const QPointF yEdge
        = m_documentToLocal(control.documentCenter + QPointF(0.0, control.documentRadius));
    const qreal xRadius = std::hypot(xEdge.x() - center.x(), xEdge.y() - center.y());
    const qreal yRadius = std::hypot(yEdge.x() - center.x(), yEdge.y() - center.y());
    return { center, (xRadius + yRadius) * 0.5 };
}

int CanvasParameterOverlayWidget::hitTest(const QPointF& localPosition) const
{
    if (!m_documentToLocal) {
        return -1;
    }
    // Last control is visually on top and therefore wins overlapping hits.
    for (int i = m_controls.size() - 1; i >= 0; --i) {
        const ScreenControl screen = screenControl(m_controls.at(i));
        if (m_controls.at(i).type == CanvasParameterControlType::Position) {
            const qreal halfSize = (kParameterPositionSize + kParameterPositionHoverGrowth) * 0.5;
            if (std::abs(localPosition.x() - screen.center.x()) <= halfSize
                && std::abs(localPosition.y() - screen.center.y()) <= halfSize) {
                return i;
            }
            continue;
        }
        const qreal pointerRadius = std::hypot(
            localPosition.x() - screen.center.x(), localPosition.y() - screen.center.y());
        if (std::abs(pointerRadius - screen.radius) <= kHitHalfWidthPx) {
            return i;
        }
    }
    return -1;
}

void CanvasParameterOverlayWidget::setHoveredControl(int index)
{
    const int resolved = index >= 0 && index < m_controls.size() ? index : -1;
    if (m_hoveredControl == resolved) {
        return;
    }
    m_hoveredControl = resolved;
    m_hoverAnimation->stop();
    if (resolved >= 0) {
        if (m_hoverVisualControl != resolved) {
            m_hoverVisualControl = resolved;
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

CanvasParameterOverlayWidget::ScreenControl CanvasParameterOverlayWidget::screenControlAt(
    int index) const
{
    return index >= 0 && index < m_controls.size() ? screenControl(m_controls.at(index))
                                                   : ScreenControl {};
}

qreal CanvasParameterOverlayWidget::hoverProgress(int index) const
{
    return index == m_hoverVisualControl ? m_hoverProgress : 0.0;
}

void CanvasParameterOverlayWidget::notifyPresentationChanged()
{
    if (m_presentationChanged) {
        m_presentationChanged();
    }
}

} // namespace ruwa::ui::workspace
