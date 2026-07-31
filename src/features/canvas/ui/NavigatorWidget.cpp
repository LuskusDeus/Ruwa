// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   N A V I G A T O R   W I D G E T
// ==========================================================================

#include "NavigatorWidget.h"

#include "CanvasPanel.h"
#include "OverviewCache.h"
#include "shared/style/WidgetStyleManager.h"

#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ruwa::ui::workspace {
namespace {

bool canNavigateCamera(const CanvasPanel* panel)
{
    return panel && panel->isGLContentReady() && panel->isInteractionEnabled()
        && !panel->isExportMode();
}

} // namespace

NavigatorWidget::NavigatorWidget(QWidget* parent)
    : QWidget(parent)
    , m_overviewCache(new OverviewCache())
{
    setMinimumSize(120, 80);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);

    m_viewportSyncTimer = new QTimer(this);
    m_viewportSyncTimer->setInterval(26);
    connect(m_viewportSyncTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible() || !m_canvasPanel || !m_canvasPanel->isGLContentReady()) {
            return;
        }

        bool overviewChanged = false;
        if (m_overviewCache->hasActiveTransitions()) {
            m_overviewCache->advanceTransitions();
            overviewChanged = true;
            if (!m_overviewCache->hasActiveTransitions()) {
                if (m_overviewCache->hasDirtyTiles()) {
                    scheduleOverviewRefresh();
                } else {
                    emit presentationReadyChanged();
                }
            }
        }

        // Repaint only when the drawn viewport outline actually moved. This poll used
        // to call update() unconditionally ~38 times a second for the widget's whole
        // life: cheap while the panel is docked, but a repaint inside a *floating*
        // panel is far more expensive, and it ran right through every brush stroke.
        // Overview content changes normally repaint through refreshOverview(); this
        // timer also drives the short dirty-tile fade while one is active.
        const QPolygonF outline = viewportOutline();
        if (!overviewChanged && outline == m_lastViewportOutline) {
            return;
        }
        m_lastViewportOutline = outline;
        update();
    });

    m_overviewRefreshTimer = new QTimer(this);
    m_overviewRefreshTimer->setSingleShot(true);
    m_overviewRefreshTimer->setInterval(32);
    connect(m_overviewRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible() || !m_canvasPanel || !m_canvasPanel->isGLContentReady()) {
            return;
        }
        if (m_canvasPanel->isDrawingActive()) {
            m_overviewRefreshTimer->start();
            return;
        }
        refreshOverview();
    });
}

NavigatorWidget::~NavigatorWidget()
{
    delete m_overviewCache;
}

void NavigatorWidget::setCanvasPanel(CanvasPanel* panel)
{
    if (m_canvasPanel == panel) {
        return;
    }
    if (m_canvasPanel) {
        disconnect(m_canvasPanel, nullptr, this, nullptr);
    }

    m_canvasPanel = panel;
    if (m_canvasPanel) {
        connect(m_canvasPanel, &CanvasPanel::glContentReady, this,
            &NavigatorWidget::scheduleOverviewRefresh, Qt::UniqueConnection);
        connect(m_canvasPanel, &CanvasPanel::canvasContentChanged, this,
            &NavigatorWidget::scheduleOverviewRefresh, Qt::UniqueConnection);
        connect(m_canvasPanel, &CanvasPanel::canvasContentTilesChanged, this,
            &NavigatorWidget::invalidateOverviewTiles, Qt::UniqueConnection);
        connect(m_canvasPanel, &CanvasPanel::canvasSizeChanged, this,
            &NavigatorWidget::scheduleOverviewRefresh, Qt::UniqueConnection);
        connect(m_canvasPanel, &CanvasPanel::exportFrameChanged, this,
            &NavigatorWidget::scheduleOverviewRefresh, Qt::UniqueConnection);
    }

    m_overviewCache->clear();
    updateOverview();
    update();
    if (isVisible() && m_canvasPanel) {
        m_viewportSyncTimer->start();
    }
}

void NavigatorWidget::refreshOverview()
{
    if (m_overviewRefreshTimer) {
        m_overviewRefreshTimer->stop();
    }
    updateOverview();
    update();
    if (presentationReady()) {
        emit presentationReadyChanged();
    }
}

bool NavigatorWidget::presentationReady() const
{
    return m_canvasPanel && m_canvasPanel->isGLContentReady() && m_overviewCache
        && m_overviewCache->isValid() && !m_overviewCache->hasDirtyTiles()
        && !m_overviewCache->hasActiveTransitions();
}

void NavigatorWidget::invalidateOverviewTiles(const QList<QPoint>& tilePositions)
{
    if (!m_canvasPanel || !m_canvasPanel->isGLContentReady()) {
        return;
    }

    const QRect currentFrame = m_canvasPanel->navigatorDisplayFrame().normalized();
    const QSize overviewSize = targetOverviewSize();
    if (!currentFrame.isValid() || currentFrame.isEmpty() || !overviewSize.isValid()) {
        if (m_overviewCache->isValid()) {
            m_overviewCache->invalidateAll();
        }
        return;
    }

    if (m_overviewCache->configure(currentFrame, overviewSize)) {
        m_overviewCache->invalidateAll();
    } else {
        if (m_canvasPanel->isDrawingActive()) {
            m_overviewCache->invalidateAll();
        } else {
            m_overviewCache->invalidateCompositionTiles(tilePositions);
        }
    }

    if (isVisible()) {
        scheduleOverviewRefresh();
    }
}

void NavigatorWidget::invalidateAllOverview()
{
    if (!m_canvasPanel || !m_canvasPanel->isGLContentReady()) {
        return;
    }

    const QRect currentFrame = m_canvasPanel->navigatorDisplayFrame().normalized();
    const QSize overviewSize = targetOverviewSize();
    if (!currentFrame.isValid() || currentFrame.isEmpty() || !overviewSize.isValid()) {
        if (m_overviewCache->isValid()) {
            m_overviewCache->invalidateAll();
        }
        return;
    }

    m_overviewCache->configure(currentFrame, overviewSize);
    m_overviewCache->invalidateAll();

    if (isVisible()) {
        scheduleOverviewRefresh();
    }
}

void NavigatorWidget::scheduleOverviewRefresh()
{
    if (!m_canvasPanel || !isVisible() || !m_overviewRefreshTimer) {
        return;
    }

    if (!m_overviewRefreshTimer->isActive()) {
        m_overviewRefreshTimer->start();
    }
}

void NavigatorWidget::updateOverview()
{
    if (!m_canvasPanel) {
        m_overviewCache->clear();
        return;
    }

    const QRect frame = m_canvasPanel->navigatorDisplayFrame().normalized();
    const QSize overviewSize = targetOverviewSize();
    if (!frame.isValid() || frame.isEmpty() || !overviewSize.isValid()) {
        m_overviewCache->clear();
        return;
    }

    if (m_overviewCache->configure(frame, overviewSize)) {
        m_overviewCache->invalidateAll();
    }
    const QList<QPoint> dirtyTiles = m_overviewCache->dirtyTiles();
    for (const QPoint& tileCoord : dirtyTiles) {
        const QRect pixelRect = m_overviewCache->overviewTilePixelRect(tileCoord);
        const QRect worldRect = m_overviewCache->worldRectForOverviewPixelRect(pixelRect);
        if (!pixelRect.isValid() || pixelRect.isEmpty() || !worldRect.isValid()
            || worldRect.isEmpty()) {
            continue;
        }

        const QImage tileImage
            = m_canvasPanel->renderNavigatorOverviewTile(worldRect, pixelRect.size());
        if (tileImage.isNull()) {
            continue;
        }
        m_overviewCache->storeTile(tileCoord, tileImage);
    }
}

QSize NavigatorWidget::targetOverviewSize() const
{
    const int maxSize = qMax(256, qMin(width(), height()) * 2);
    QSize size(maxSize, maxSize);
    if (m_canvasPanel) {
        const QRect frame = m_canvasPanel->navigatorDisplayFrame().normalized();
        if (frame.isValid() && !frame.isEmpty()) {
            size = frame.size();
            size.scale(maxSize, maxSize, Qt::KeepAspectRatio);
        }
    }
    return size;
}

QRectF NavigatorWidget::canvasDisplayRect() const
{
    if (!m_canvasPanel || !m_overviewCache->isValid()) {
        return {};
    }

    const QRectF frame = presentedWorldFrame();
    if (!frame.isValid() || frame.isEmpty()) {
        return {};
    }

    const float cw = static_cast<float>(frame.width());
    const float ch = static_cast<float>(frame.height());
    const float w = static_cast<float>(width());
    const float h = static_cast<float>(height());
    const float scale = qMin(w / cw, h / ch);
    const float sw = cw * scale;
    const float sh = ch * scale;
    const float ox = (w - sw) * 0.5f;
    const float oy = (h - sh) * 0.5f;
    return QRectF(ox, oy, sw, sh);
}

QPointF NavigatorWidget::widgetToWorld(const QPointF& widgetPos) const
{
    const QRectF disp = canvasDisplayRect();
    if (disp.isEmpty() || !m_canvasPanel || !m_canvasPanel->isGLContentReady()) {
        return {};
    }
    const QRectF frame = presentedWorldFrame();
    if (!frame.isValid() || frame.isEmpty()) {
        return {};
    }

    const float nx = (widgetPos.x() - disp.x()) / disp.width();
    const float ny = (widgetPos.y() - disp.y()) / disp.height();
    return QPointF(static_cast<float>(frame.x()) + nx * static_cast<float>(frame.width()),
        static_cast<float>(frame.y()) + ny * static_cast<float>(frame.height()));
}

QPointF NavigatorWidget::worldToWidget(const QPointF& worldPos) const
{
    const QRectF disp = canvasDisplayRect();
    const QRectF frame = presentedWorldFrame();
    if (disp.isEmpty() || !frame.isValid() || frame.isEmpty()) {
        return {};
    }

    const float nx
        = static_cast<float>(worldPos.x() - frame.x()) / static_cast<float>(frame.width());
    const float ny
        = static_cast<float>(worldPos.y() - frame.y()) / static_cast<float>(frame.height());
    return QPointF(disp.x() + nx * disp.width(), disp.y() + ny * disp.height());
}

void NavigatorWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto& colors = ruwa::ui::core::WidgetStyleManager::instance().colors();
    p.fillRect(rect(), colors.surface);

    // Recorded even when nothing below gets drawn, so the viewport sync poll never
    // sees a permanently stale key and repaints on every tick.
    const QPolygonF outline = viewportOutline();
    m_lastViewportOutline = outline;

    const QRectF disp = canvasDisplayRect();
    if (disp.isEmpty()) {
        return;
    }

    m_overviewCache->draw(p, disp);

    if (!outline.isEmpty()) {
        p.setPen(QPen(colors.primary, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(outline);
    }
}

QPolygonF NavigatorWidget::viewportOutline() const
{
    if (!m_canvasPanel || !m_canvasPanel->isGLContentReady()) {
        return {};
    }

    const auto& vp = m_canvasPanel->viewport();
    const float vw = static_cast<float>(vp.width());
    const float vh = static_cast<float>(vp.height());
    const QRectF frame = presentedWorldFrame();
    if (vw < 1 || vh < 1 || !frame.isValid() || frame.isEmpty()) {
        return {};
    }

    const aether::Vector2 p0 = vp.screenToWorld({ 0.0f, 0.0f });
    const aether::Vector2 p1 = vp.screenToWorld({ vw, 0.0f });
    const aether::Vector2 p2 = vp.screenToWorld({ vw, vh });
    const aether::Vector2 p3 = vp.screenToWorld({ 0.0f, vh });

    QPolygonF poly;
    poly << worldToWidget(QPointF(p0.x, p0.y)) << worldToWidget(QPointF(p1.x, p1.y))
         << worldToWidget(QPointF(p2.x, p2.y)) << worldToWidget(QPointF(p3.x, p3.y));
    return poly;
}

QRectF NavigatorWidget::presentedWorldFrame() const
{
    return m_overviewCache ? m_overviewCache->presentedWorldFrame() : QRectF {};
}

void NavigatorWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && canNavigateCamera(m_canvasPanel)) {
        m_dragging = true;
        m_dragStartPos = event->pos();
        auto& cam = m_canvasPanel->viewport().camera();
        cam.stopAnimation();
        m_dragStartCameraCenter = QPointF(cam.position().x, cam.position().y);
    }
    QWidget::mousePressEvent(event);
}

void NavigatorWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && !canNavigateCamera(m_canvasPanel)) {
        m_dragging = false;
    }
    if (m_dragging && canNavigateCamera(m_canvasPanel)) {
        const QPointF worldNow = widgetToWorld(event->pos());
        const QPointF worldStart = widgetToWorld(m_dragStartPos);
        const float dx = worldNow.x() - worldStart.x();
        const float dy = worldNow.y() - worldStart.y();

        auto& cam = m_canvasPanel->viewport().camera();
        cam.setPosition(m_dragStartCameraCenter.x() + dx, m_dragStartCameraCenter.y() + dy);
        m_canvasPanel->requestRender();
    }
    QWidget::mouseMoveEvent(event);
}

void NavigatorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging && !canNavigateCamera(m_canvasPanel)) {
        m_dragging = false;
    }
    if (event->button() == Qt::LeftButton && canNavigateCamera(m_canvasPanel)) {
        if (m_dragging) {
            const QPoint delta = event->pos() - m_dragStartPos;
            if (delta.manhattanLength() < 5) {
                const QPointF world = widgetToWorld(event->pos());
                auto& cam = m_canvasPanel->viewport().camera();
                cam.centerOn(aether::Vector2 {
                    static_cast<float>(world.x()), static_cast<float>(world.y()) });
                m_canvasPanel->requestRender();
            }
            m_dragging = false;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void NavigatorWidget::wheelEvent(QWheelEvent* event)
{
    if (!canNavigateCamera(m_canvasPanel)) {
        QWidget::wheelEvent(event);
        return;
    }
    // Prefer vertical wheel delta; some devices send horizontal delta while Alt is held.
    int zoomDelta = event->angleDelta().y();
    if (zoomDelta == 0) {
        zoomDelta = event->angleDelta().x();
    }
    if (zoomDelta == 0 && !event->pixelDelta().isNull()) {
        zoomDelta = event->pixelDelta().y();
        if (zoomDelta == 0) {
            zoomDelta = event->pixelDelta().x();
        }
    }
    if (zoomDelta == 0) {
        event->accept();
        return;
    }
    const float exponent = std::clamp(zoomDelta / 120.0f, -5.0f, 5.0f);
    const float zoomFactor = std::pow(1.15f, exponent);
    const QPointF worldPos = widgetToWorld(event->position());
    m_canvasPanel->zoomAtWorldPoint(zoomFactor, worldPos);
    event->accept();
}

void NavigatorWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_canvasPanel) {
        if (!m_overviewCache->isValid()) {
            QTimer::singleShot(100, this, [this]() { refreshOverview(); });
        } else {
            scheduleOverviewRefresh();
        }
        m_viewportSyncTimer->start();
    }
}

void NavigatorWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_viewportSyncTimer->stop();
    if (m_overviewRefreshTimer) {
        m_overviewRefreshTimer->stop();
    }
}

void NavigatorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    scheduleOverviewRefresh();
}

} // namespace ruwa::ui::workspace
