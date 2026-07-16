// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   V I E W   C O N T R O L L E R
// ==========================================================================

#include "CanvasViewController.h"

#include "CanvasPanel.h"
#include "CanvasCursorManager.h"
#include "CanvasOverlayLayoutManager.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/brush/ui/BrushSizeCurve.h"
#include "features/canvas-resize/CanvasResizeController.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/canvas/scene/ZoomLimits.h"
#include "features/canvas/ui/CanvasBrushQuickPopup.h"
#include "features/canvas/ui/CanvasStylusJoystickContainerWidget.h"
#include "features/canvas/ui/CanvasToolStateOverlay.h"
#include "features/canvas/ui/CanvasSelectionSizeOverlay.h"
#include "features/canvas/ui/CanvasZoomInfoOverlay.h"
#include "features/canvas/ui/ImageImportSelectionOverlay.h"
#include "features/color/ColorPicker.h"
#include "features/color/ColorPickerOverlay.h"
#include "features/export/ExportSettingsPanel.h"
#include "features/selection/SelectionActionPopup.h"
#include "services/input/StylusInputManager.h"
#include "shared/undo/UndoManager.h"
#include "shared/widgets/overlays/ConfirmationPopup.h"
#include "TextEditingController.h"

#include <QList>
#include <QRect>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace ruwa::ui::workspace {
namespace {

float effectiveMaxBrushRadiusForCanvas(
    int canvasWidth, int canvasHeight, bool hasFiniteDocumentBounds)
{
    return ruwa::ui::widgets::maxBrushRadiusForCanvasMode(
        canvasWidth, canvasHeight, hasFiniteDocumentBounds);
}

} // namespace

CanvasViewController::CanvasViewController(CanvasPanel* panel)
    : m_panel(panel)
{
}

aether::Viewport& CanvasViewController::viewport()
{
    return m_panel->m_glWidget->viewport();
}

const aether::Viewport& CanvasViewController::viewport() const
{
    return m_panel->m_glWidget->viewport();
}

aether::Canvas& CanvasViewController::canvas()
{
    return m_panel->m_glWidget->canvas();
}

const aether::Canvas& CanvasViewController::canvas() const
{
    return m_panel->m_glWidget->canvas();
}

aether::UndoManager* CanvasViewController::undoManagerOrNull()
{
    return m_panel->m_glWidget ? &m_panel->m_glWidget->canvas().undoManager() : nullptr;
}

aether::UndoManager* CanvasViewController::activeUndoManagerOrNull()
{
    return m_panel->m_glWidget ? m_panel->m_glWidget->activeUndoManager() : nullptr;
}

void CanvasViewController::setZoom(float zoom)
{
    if (!m_panel->isInteractionEnabled() || m_panel->isExportMode()) {
        return;
    }
    if (m_panel->m_glWidget) {
        m_panel->m_glWidget->viewport().camera().setZoom(zoom);
        emit m_panel->zoomChanged(static_cast<qreal>(zoom));
        showZoomInfoOverlay();
        m_panel->requestRender();
    }
}

void CanvasViewController::setZoomSmooth(float zoom)
{
    if (!m_panel->isInteractionEnabled() || m_panel->isExportMode()) {
        return;
    }
    if (m_panel->m_glWidget) {
        auto& vp = m_panel->m_glWidget->viewport();
        vp.camera().setZoomSmooth(zoom, vp.size());
        emit m_panel->zoomChanged(static_cast<qreal>(vp.camera().zoom()));
        showZoomInfoOverlay();
        m_panel->requestRender();
    }
}

void CanvasViewController::zoomBy(float factor)
{
    if (!m_panel->isInteractionEnabled() || m_panel->isExportMode()) {
        return;
    }
    if (m_panel->m_glWidget) {
        auto& vp = m_panel->m_glWidget->viewport();
        auto& cam = vp.camera();
        aether::Vector2 viewportSize = vp.size();
        aether::Vector2 centerScreen(viewportSize.x * 0.5f, viewportSize.y * 0.5f);
        cam.zoomAtSmooth(factor, centerScreen, viewportSize);
        emit m_panel->zoomChanged(static_cast<qreal>(cam.zoom()));
        showZoomInfoOverlay();
        m_panel->requestRender();
    }
}

void CanvasViewController::zoomAtWorldPoint(float factor, const QPointF& worldPos)
{
    if (!m_panel->isInteractionEnabled() || m_panel->isExportMode()) {
        return;
    }
    if (m_panel->m_glWidget) {
        auto& vp = m_panel->m_glWidget->viewport();
        auto& cam = vp.camera();
        aether::Vector2 viewportSize = vp.size();
        aether::Vector2 screenPoint = m_panel->m_glWidget->screenFromDocumentWorld(
            aether::Vector2 { static_cast<float>(worldPos.x()), static_cast<float>(worldPos.y()) });
        cam.zoomAtSmooth(factor, screenPoint, viewportSize);
        emit m_panel->zoomChanged(static_cast<qreal>(cam.zoom()));
        showZoomInfoOverlay();
        m_panel->requestRender();
    }
}

void CanvasViewController::zoomToFit()
{
    if (!m_panel->isInteractionEnabled() || m_panel->isExportMode()) {
        return;
    }
    if (m_panel->m_glWidget) {
        m_panel->m_glWidget->endPanSampling();
        m_panel->m_isPanning = false;
        m_panel->m_isZoomDragging = false;
        m_panel->m_isRotatingView = false;
        auto& vp = m_panel->m_glWidget->viewport();
        auto& cam = vp.camera();
        const QRect displayFrame = m_panel->effectiveDisplayFrame();
        const aether::Vector2 fitSize { static_cast<float>(qMax(1, displayFrame.width())),
            static_cast<float>(qMax(1, displayFrame.height())) };
        cam.fitToCanvasSmooth(fitSize, vp.size(), 50.0f);
        cam.centerOnSmooth(aether::Vector2 { static_cast<float>(displayFrame.center().x()) + 0.5f,
            static_cast<float>(displayFrame.center().y()) + 0.5f });
        emit m_panel->zoomChanged(static_cast<qreal>(cam.zoom()));
        showZoomInfoOverlay();
        m_panel->requestRender();
    }
}

void CanvasViewController::toggleCanvasViewFlipHorizontal()
{
    if (m_panel->m_glWidget) {
        m_panel->m_glWidget->toggleCanvasContentFlipHorizontal();
        if (m_panel->m_toolStateOverlay) {
            m_panel->m_toolStateOverlay->setCanvasFlipStates(
                m_panel->m_glWidget->canvasContentFlipHorizontal(),
                m_panel->m_glWidget->canvasContentFlipVertical());
        }
    }
}

void CanvasViewController::toggleCanvasViewFlipVertical()
{
    if (m_panel->m_glWidget) {
        m_panel->m_glWidget->toggleCanvasContentFlipVertical();
        if (m_panel->m_toolStateOverlay) {
            m_panel->m_toolStateOverlay->setCanvasFlipStates(
                m_panel->m_glWidget->canvasContentFlipHorizontal(),
                m_panel->m_glWidget->canvasContentFlipVertical());
        }
    }
}

void CanvasViewController::centerCanvas()
{
    if (!m_panel->isInteractionEnabled() || m_panel->isExportMode()) {
        return;
    }
    if (m_panel->m_glWidget) {
        m_panel->m_glWidget->viewport().camera().centerOn(m_panel->m_glWidget->canvas().center());
        m_panel->requestRender();
    }
}

void CanvasViewController::applyZoomLimits()
{
    const float maxBrush = effectiveMaxBrushRadiusForCanvas(m_panel->m_canvasSize.width(),
        m_panel->m_canvasSize.height(), m_panel->hasFiniteDocumentBounds());
    const bool ignoreMinZoom = m_panel->m_exportModeOverlayProgress > 1e-5;

    if (!m_panel->m_glWidget) {
        if (m_panel->m_stylusJoystick) {
            const auto [minZoom, maxZoom]
                = ruwa::core::canvas::computeZoomLimits(800, 800, maxBrush);
            m_panel->m_stylusJoystick->setZoomLimits(
                ignoreMinZoom ? 0.001 : static_cast<qreal>(minZoom), maxZoom);
        }
        return;
    }
    const auto vpSize = m_panel->m_glWidget->viewport().size();
    const auto [minZoom, maxZoom] = ruwa::core::canvas::computeZoomLimits(
        static_cast<int>(vpSize.x), static_cast<int>(vpSize.y), maxBrush);
    const float effectiveMinZoom = ignoreMinZoom ? 0.001f : static_cast<float>(minZoom);
    m_panel->m_glWidget->viewport().camera().setZoomLimits(
        effectiveMinZoom, static_cast<float>(maxZoom));
    if (m_panel->m_stylusJoystick) {
        m_panel->m_stylusJoystick->setZoomLimits(effectiveMinZoom, maxZoom);
        if (m_panel->m_glWidget->isInitialized()) {
            m_panel->m_stylusJoystick->setZoom(
                static_cast<qreal>(m_panel->m_glWidget->viewport().camera().zoom()));
            m_panel->m_stylusJoystick->setRotation(
                static_cast<qreal>(m_panel->m_glWidget->viewport().camera().rotation()));
        }
    }
}

void CanvasViewController::showZoomInfoOverlay()
{
    if (!m_panel->m_zoomInfoOverlay || !m_panel->m_glWidget
        || !m_panel->m_glWidget->isInitialized()) {
        return;
    }

    m_panel->m_zoomInfoOverlay->showZoom(
        static_cast<qreal>(m_panel->m_glWidget->viewport().camera().zoom()));
    positionZoomInfoOverlay();
    m_panel->m_zoomInfoOverlay->raise();
}

void CanvasViewController::syncZoomInfoOverlayValue()
{
    if (!m_panel->m_zoomInfoOverlay || !m_panel->m_glWidget
        || !m_panel->m_glWidget->isInitialized()) {
        return;
    }

    const qreal zoom = static_cast<qreal>(m_panel->m_glWidget->viewport().camera().zoom());
    if (m_panel->m_zoomInfoOverlay->isPresentationActive()) {
        m_panel->m_zoomInfoOverlay->showZoom(zoom);
        positionZoomInfoOverlay();
        m_panel->m_zoomInfoOverlay->raise();
    } else {
        m_panel->m_zoomInfoOverlay->setZoom(zoom);
        positionZoomInfoOverlay();
    }
}

void CanvasViewController::positionZoomInfoOverlay()
{
    if (!m_panel->m_zoomInfoOverlay || !m_panel->m_contentWidget) {
        return;
    }

    const int contentWidth = m_panel->m_contentWidget->width();
    const int contentHeight = m_panel->m_contentWidget->height();
    if (contentWidth <= 0 || contentHeight <= 0) {
        return;
    }

    m_panel->m_zoomInfoOverlay->adjustSize();

    const int margin = kCanvasOverlayEdgeMargin;
    const int spacing = kCanvasOverlayEdgeMargin;
    const int overlayWidth = m_panel->m_zoomInfoOverlay->width();
    const int overlayHeight = m_panel->m_zoomInfoOverlay->height();
    const int maxX = contentWidth - overlayWidth - margin;
    const int maxY = contentHeight - overlayHeight - margin;
    const int x = qBound(margin, (contentWidth - overlayWidth) / 2, qMax(margin, maxX));

    QList<QRect> occupiedRects;
    const auto addOccupiedRect = [this, &occupiedRects, spacing](const QWidget* widget) {
        if (!widget || widget == m_panel->m_zoomInfoOverlay || !widget->isVisible()) {
            return;
        }
        if (widget->parentWidget() != m_panel->m_contentWidget) {
            return;
        }
        const QRect rect = widget->geometry();
        if (!rect.isValid() || rect.isEmpty()) {
            return;
        }
        occupiedRects.append(rect.adjusted(-spacing, -spacing, spacing, spacing));
    };

    addOccupiedRect(m_panel->m_toolStateOverlay);
    addOccupiedRect(m_panel->m_brushOverlay);
    addOccupiedRect(m_panel->m_stylusJoystick);
    addOccupiedRect(m_panel->m_exportPanel);
    addOccupiedRect(m_panel->m_confirmationPopup);
    addOccupiedRect(m_panel->m_selectionActionPopup);
    addOccupiedRect(m_panel->m_selectionColorPickerOverlay
            ? m_panel->m_selectionColorPickerOverlay->picker()
            : nullptr);
    addOccupiedRect(m_panel->m_brushQuickPopup);
    addOccupiedRect(m_panel->m_imageImportSelectionOverlay);

    std::sort(occupiedRects.begin(), occupiedRects.end(), [](const QRect& lhs, const QRect& rhs) {
        return lhs.top() == rhs.top() ? lhs.left() < rhs.left() : lhs.top() < rhs.top();
    });

    int y = margin;
    for (int i = 0; i <= occupiedRects.size(); ++i) {
        bool moved = false;
        const QRect candidate(x, y, overlayWidth, overlayHeight);
        for (const QRect& occupied : occupiedRects) {
            if (!candidate.intersects(occupied)) {
                continue;
            }
            y = occupied.bottom() + 1 + spacing;
            moved = true;
            break;
        }
        if (!moved) {
            break;
        }
    }

    y = qBound(margin, y, qMax(margin, maxY));
    m_panel->m_zoomInfoOverlay->move(x, y);
}

void CanvasViewController::updateSelectionSizeOverlay()
{
    if (!m_panel->m_selectionSizeOverlay || !m_panel->m_glWidget || !m_panel->m_contentWidget) {
        return;
    }

    QRectF worldRect;
    if (!m_panel->m_glWidget->liveRectSelectionBoundsWorld(worldRect)) {
        // Drag hasn't produced any geometry yet — nothing to report.
        return;
    }

    const int widthPx = qRound(worldRect.width());
    const int heightPx = qRound(worldRect.height());

    // Map the four corners into content-panel space and take their bounding box,
    // so the anchor stays correct under a flipped or rotated view.
    const auto toPanel = [this](qreal wx, qreal wy) {
        return mapWorldToPanel(aether::Vector2(static_cast<float>(wx), static_cast<float>(wy)));
    };
    const QPointF p0 = toPanel(worldRect.left(), worldRect.top());
    const QPointF p1 = toPanel(worldRect.right(), worldRect.top());
    const QPointF p2 = toPanel(worldRect.right(), worldRect.bottom());
    const QPointF p3 = toPanel(worldRect.left(), worldRect.bottom());
    const qreal minX = std::min({ p0.x(), p1.x(), p2.x(), p3.x() });
    const qreal maxX = std::max({ p0.x(), p1.x(), p2.x(), p3.x() });
    const qreal minY = std::min({ p0.y(), p1.y(), p2.y(), p3.y() });
    const qreal maxY = std::max({ p0.y(), p1.y(), p2.y(), p3.y() });
    const QRectF panelRect(QPointF(minX, minY), QPointF(maxX, maxY));

    m_panel->m_selectionSizeOverlay->present(widthPx, heightPx, panelRect);
    m_panel->m_selectionSizeOverlay->raise();
}

void CanvasViewController::hideSelectionSizeOverlay()
{
    if (m_panel->m_selectionSizeOverlay) {
        m_panel->m_selectionSizeOverlay->dismiss();
    }
}

bool CanvasViewController::handleWheelZoom(QWheelEvent* event)
{
    if (!m_panel->isInteractionEnabled() || !m_panel->m_glWidget
        || !m_panel->m_glWidget->isInitialized()) {
        return false;
    }

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
        return true;
    }

    const float exponent = std::clamp(zoomDelta / 120.0f, -5.0f, 5.0f);
    const float zoomFactor = std::pow(1.15f, exponent);

    const bool directWinTabRouting
        = ruwa::services::input::StylusInputManager::instance().usesNativeUiRouting();
    const QPoint globalPos = directWinTabRouting && m_panel->m_cursorManager
        ? m_panel->m_cursorManager->activeCursorPosition()
        : event->globalPosition().toPoint();
    QPoint localPos = m_panel->m_glWidget->mapFromGlobal(globalPos);
    aether::Vector2 screenPoint(localPos.x(), localPos.y());
    aether::Vector2 viewportSize(
        m_panel->m_glWidget->viewport().width(), m_panel->m_glWidget->viewport().height());

    m_panel->m_glWidget->viewport().camera().zoomAtSmooth(zoomFactor, screenPoint, viewportSize);

    emit m_panel->zoomChanged(static_cast<qreal>(m_panel->m_glWidget->viewport().camera().zoom()));
    showZoomInfoOverlay();
    m_panel->requestRender();
    if (m_panel->m_canvasResizeController
        && m_panel->m_canvasResizeController->isInteractionActive()) {
        m_panel->m_canvasResizeController->updateOverlay();
    }
    if (m_panel->m_textEditingController && m_panel->m_textEditingController->isEditing()) {
        m_panel->m_textEditingController->refreshFormattingPopup();
    }

    event->accept();
    return true;
}

aether::Vector2 CanvasViewController::mapToWorld(const QPoint& globalPos) const
{
    return mapToWorld(QPointF(globalPos));
}

aether::Vector2 CanvasViewController::mapToViewportWorld(const QPoint& globalPos) const
{
    return mapToViewportWorld(QPointF(globalPos));
}

aether::Vector2 CanvasViewController::mapToWorld(const QPointF& globalPos) const
{
    if (!m_panel->m_glWidget)
        return aether::Vector2 { 0.0f, 0.0f };
    QPointF localPos = m_panel->m_glWidget->mapFromGlobal(globalPos);
    aether::Vector2 screenPos(static_cast<float>(localPos.x()), static_cast<float>(localPos.y()));
    return m_panel->m_glWidget->documentWorldFromScreen(screenPos);
}

aether::Vector2 CanvasViewController::mapToViewportWorld(const QPointF& globalPos) const
{
    if (!m_panel->m_glWidget) {
        return aether::Vector2 { 0.0f, 0.0f };
    }

    const QPointF localPos = m_panel->m_glWidget->mapFromGlobal(globalPos);
    float sx = static_cast<float>(localPos.x());
    float sy = static_cast<float>(localPos.y());
    const int viewportWidth = m_panel->m_glWidget->width();
    const int viewportHeight = m_panel->m_glWidget->height();
    if (viewportWidth > 0 && viewportHeight > 0) {
        const float maxX = static_cast<float>(std::max(0, viewportWidth - 1));
        const float maxY = static_cast<float>(std::max(0, viewportHeight - 1));
        sx = std::clamp(sx, 0.0f, maxX);
        sy = std::clamp(sy, 0.0f, maxY);
    }

    return m_panel->m_glWidget->documentWorldFromScreen({ sx, sy });
}

bool CanvasViewController::isGlobalOverGlViewport(const QPoint& globalPos) const
{
    return isGlobalOverGlViewport(QPointF(globalPos));
}

bool CanvasViewController::isGlobalOverGlViewport(const QPointF& globalPos) const
{
    if (!m_panel->m_glWidget) {
        return false;
    }

    const int viewportWidth = m_panel->m_glWidget->width();
    const int viewportHeight = m_panel->m_glWidget->height();
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return false;
    }

    const QPointF localPos = m_panel->m_glWidget->mapFromGlobal(globalPos);
    return localPos.x() >= 0.0 && localPos.y() >= 0.0
        && localPos.x() < static_cast<qreal>(viewportWidth)
        && localPos.y() < static_cast<qreal>(viewportHeight);
}

QPointF CanvasViewController::mapWorldToPanel(const aether::Vector2& worldPos) const
{
    if (!m_panel->m_glWidget) {
        return QPointF();
    }
    const aether::Vector2 screenPos = m_panel->m_glWidget->screenFromDocumentWorld(worldPos);
    QWidget* anchorParent = m_panel->m_contentWidget ? m_panel->m_contentWidget : m_panel;
    const QPoint glTopLeft = m_panel->m_glWidget->mapTo(anchorParent, QPoint(0, 0));
    return QPointF(static_cast<qreal>(glTopLeft.x()) + static_cast<qreal>(screenPos.x),
        static_cast<qreal>(glTopLeft.y()) + static_cast<qreal>(screenPos.y));
}

} // namespace ruwa::ui::workspace
