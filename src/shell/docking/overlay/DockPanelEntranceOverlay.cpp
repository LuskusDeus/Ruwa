// SPDX-License-Identifier: MPL-2.0

#include "DockPanelEntranceOverlay.h"

#include "shell/docking/core/DockContainerWidget.h"
#include "shell/docking/core/DockFloatingContainer.h"
#include "shell/docking/layout/DockSplitHandle.h"
#include "shell/docking/widgets/DockPanel.h"

#include <QEvent>
#include <QEasingCurve>
#include <QPainter>
#include <QRegion>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>
#include <array>
#include <utility>

namespace ruwa::ui::docking {

DockPanelEntranceOverlay::DockPanelEntranceOverlay(
    DockContainerWidget* container, DockPanel* stationaryPanel, const QColor& backgroundColor)
    : QWidget(container)
    , m_container(container)
    , m_backgroundColor(backgroundColor)
{
    setObjectName(QStringLiteral("dockPanelEntranceOverlay"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    setGeometry(container ? container->rect() : QRect());

    if (!m_container || !stationaryPanel || !rect().isValid()) {
        return;
    }

    const QRect stationaryRect(
        stationaryPanel->mapTo(m_container, QPoint(0, 0)), stationaryPanel->size());
    std::array<QList<QPointer<DockPanel>>, 4> panelsByEdge;
    std::array<QRect, 4> boundsByEdge;

    const QList<DockPanel*> panels = m_container->dockedPanels();
    for (DockPanel* panel : panels) {
        if (!panel || panel == stationaryPanel || !panel->isDocked() || !panel->isVisible()) {
            continue;
        }

        const QRect panelRect(panel->mapTo(m_container, QPoint(0, 0)), panel->size());
        if (!panelRect.isValid() || panelRect.intersected(rect()).isEmpty()) {
            continue;
        }

        const EntranceEdge edge = edgeFor(panelRect, stationaryRect);
        const auto index = static_cast<std::size_t>(edge);
        panelsByEdge[index].append(panel);
        boundsByEdge[index]
            = boundsByEdge[index].isValid() ? boundsByEdge[index].united(panelRect) : panelRect;
    }

    QRegion animatedRegion;
    m_items.reserve(4);
    for (std::size_t index = 0; index < panelsByEdge.size(); ++index) {
        if (panelsByEdge[index].isEmpty() || !boundsByEdge[index].isValid()) {
            continue;
        }

        const EntranceEdge edge = static_cast<EntranceEdge>(index);
        QRect targetRect = boundsByEdge[index];
        switch (edge) {
        case EntranceEdge::Left:
            targetRect.setLeft(0);
            if (targetRect.right() < stationaryRect.left()) {
                targetRect.setRight(stationaryRect.left() - 1);
            }
            break;
        case EntranceEdge::Right:
            targetRect.setRight(width() - 1);
            if (targetRect.left() > stationaryRect.right()) {
                targetRect.setLeft(stationaryRect.right() + 1);
            }
            break;
        case EntranceEdge::Top:
            targetRect.setTop(0);
            if (targetRect.bottom() < stationaryRect.top()) {
                targetRect.setBottom(stationaryRect.top() - 1);
            }
            break;
        case EntranceEdge::Bottom:
            targetRect.setBottom(height() - 1);
            if (targetRect.top() > stationaryRect.bottom()) {
                targetRect.setTop(stationaryRect.bottom() + 1);
            }
            break;
        }
        targetRect = targetRect.intersected(rect());

        const QPixmap snapshot = captureSideSnapshot(targetRect, panelsByEdge[index]);
        if (snapshot.isNull()) {
            continue;
        }

        Item item;
        item.targetRect = targetRect;
        item.startOffset = startOffsetFor(edge, targetRect);
        item.snapshot = snapshot;
        m_items.append(std::move(item));
        animatedRegion += targetRect;
    }

    if (m_items.isEmpty()) {
        return;
    }

    // Each mask rectangle is the complete travel corridor for one optional side
    // snapshot. It reaches the outer edge but never covers the stationary canvas.
    setMask(animatedRegion);
    m_container->installEventFilter(this);
    show();
    raise();

    // Floating panels are not part of the workspace-edge entrance and must stay above it.
    for (DockFloatingContainer* floating : m_container->floatingContainers()) {
        if (floating && floating->isVisible()) {
            floating->raise();
        }
    }

    // Materialize the first covered frame before the loading shell disappears.
    repaint();
}

DockPanelEntranceOverlay::~DockPanelEntranceOverlay()
{
    if (m_container) {
        m_container->removeEventFilter(this);
    }
}

void DockPanelEntranceOverlay::start(int durationMs)
{
    if (!isReady() || m_started) {
        return;
    }

    m_started = true;
    m_animation = new QVariantAnimation(this);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setDuration(qMax(1, durationMs));
    m_animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_progress = std::clamp(value.toReal(), 0.0, 1.0);
        update();
    });
    connect(m_animation, &QVariantAnimation::finished, this, [this]() {
        m_progress = 1.0;
        repaint();
        emit finished();

        // Keep the pixel-identical final frame until the next event-loop turn.
        // The settled real panels are already painted underneath it.
        QTimer::singleShot(0, this, [this]() {
            hide();
            deleteLater();
        });
    });

    m_animation->start();
}

void DockPanelEntranceOverlay::cancel()
{
    if (m_animation) {
        m_animation->stop();
    }
    hide();
    deleteLater();
}

void DockPanelEntranceOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QColor opaqueBackground = m_backgroundColor;
    opaqueBackground.setAlpha(255);

    for (const Item& item : std::as_const(m_items)) {
        painter.fillRect(item.targetRect, opaqueBackground);

        painter.save();
        painter.setClipRect(item.targetRect, Qt::IntersectClip);
        const QPoint offset(qRound(item.startOffset.x() * (1.0 - m_progress)),
            qRound(item.startOffset.y() * (1.0 - m_progress)));
        const QRectF destination(QRect(item.targetRect.topLeft() + offset, item.targetRect.size()));
        const qreal dpr = qMax<qreal>(1.0, item.snapshot.devicePixelRatio());
        const QRectF source(
            QPointF(0.0, 0.0), QSizeF(item.snapshot.width() / dpr, item.snapshot.height() / dpr));
        painter.drawPixmap(destination, item.snapshot, source);
        painter.restore();
    }
}

bool DockPanelEntranceOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_container
        && (event->type() == QEvent::Resize || event->type() == QEvent::Hide)) {
        cancel();
    }
    return QWidget::eventFilter(watched, event);
}

QPixmap DockPanelEntranceOverlay::captureSideSnapshot(
    const QRect& targetRect, const QList<QPointer<DockPanel>>& panels) const
{
    if (!m_container || !targetRect.isValid()) {
        return {};
    }

    const qreal dpr = m_container->devicePixelRatioF();
    const QSize deviceSize(
        qMax(1, qRound(targetRect.width() * dpr)), qMax(1, qRound(targetRect.height() * dpr)));
    QPixmap snapshot(deviceSize);
    snapshot.setDevicePixelRatio(dpr);

    QColor opaqueBackground = m_backgroundColor;
    opaqueBackground.setAlpha(255);
    snapshot.fill(opaqueBackground);

    QPainter painter(&snapshot);
    const auto drawWidget = [this, &painter, &targetRect](QWidget* widget) {
        if (!widget || !widget->isVisible()) {
            return;
        }

        const QRect widgetRect(widget->mapTo(m_container, QPoint(0, 0)), widget->size());
        const QRect visibleRect = widgetRect.intersected(targetRect);
        if (visibleRect.isEmpty()) {
            return;
        }

        const QPixmap widgetSnapshot = widget->grab();
        if (widgetSnapshot.isNull()) {
            return;
        }

        const QRect localRect(visibleRect.translated(-targetRect.topLeft()));
        const QRect sourceRect(visibleRect.translated(-widgetRect.topLeft()));
        painter.drawPixmap(QRectF(localRect), widgetSnapshot, QRectF(sourceRect));
    };

    for (const QPointer<DockPanel>& panel : panels) {
        drawWidget(panel.data());
    }
    const QList<DockSplitHandle*> handles = m_container->findChildren<DockSplitHandle*>();
    for (DockSplitHandle* handle : handles) {
        drawWidget(handle);
    }

    return snapshot;
}

DockPanelEntranceOverlay::EntranceEdge DockPanelEntranceOverlay::edgeFor(
    const QRect& panelRect, const QRect& stationaryRect) const
{
    if (panelRect.right() < stationaryRect.left()) {
        return EntranceEdge::Left;
    }
    if (panelRect.left() > stationaryRect.right()) {
        return EntranceEdge::Right;
    }
    if (panelRect.bottom() < stationaryRect.top()) {
        return EntranceEdge::Top;
    }
    if (panelRect.top() > stationaryRect.bottom()) {
        return EntranceEdge::Bottom;
    }

    // Overlapping/non-standard layouts fall back to the nearest workspace edge.
    const int leftDistance = qMax(0, panelRect.left());
    const int rightDistance = qMax(0, width() - panelRect.right() - 1);
    const int topDistance = qMax(0, panelRect.top());
    const int bottomDistance = qMax(0, height() - panelRect.bottom() - 1);
    const int nearest = std::min({ leftDistance, rightDistance, topDistance, bottomDistance });

    if (nearest == leftDistance) {
        return EntranceEdge::Left;
    }
    if (nearest == rightDistance) {
        return EntranceEdge::Right;
    }
    if (nearest == topDistance) {
        return EntranceEdge::Top;
    }
    return EntranceEdge::Bottom;
}

QPoint DockPanelEntranceOverlay::startOffsetFor(EntranceEdge edge, const QRect& targetRect) const
{
    switch (edge) {
    case EntranceEdge::Left:
        return QPoint(-targetRect.right() - 1, 0);
    case EntranceEdge::Right:
        return QPoint(width() - targetRect.left(), 0);
    case EntranceEdge::Top:
        return QPoint(0, -targetRect.bottom() - 1);
    case EntranceEdge::Bottom:
        return QPoint(0, height() - targetRect.top());
    }
    return QPoint();
}

} // namespace ruwa::ui::docking
