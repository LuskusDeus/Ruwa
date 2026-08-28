// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   R E N D E R   L I F E C Y C L E
// ==========================================================================
// Controller setup and the render-session-ready choreography. All camera and
// view work goes through the renderer-neutral view capability.
// ==========================================================================

#include "CanvasPanel.h"
#include "features/canvas/engine/CanvasEngineSession.h"

#include "CanvasToolStateOverlay.h"
#include "TextEditingController.h"
#include "features/canvas-resize/CanvasResizeController.h"
#include "features/canvas/engine/CanvasEngineTypes.h"
#include "features/export/ExportAreaController.h"

#include <algorithm>

namespace ruwa::ui::workspace {

void CanvasPanel::setupCanvasResizeController()
{
    if (!m_canvasResizeController || !m_glWidget) {
        return;
    }
    // TRANSITIONAL QUARANTINE: the resize controller still takes the concrete
    // legacy renderer; it moves onto neutral dependencies in its own step.
    m_canvasResizeController->setGlWidget(m_glWidget);
    m_canvasResizeController->setLayerModel(m_layerModel);
    m_canvasResizeController->setCanvasSize(m_canvasSize);
    m_canvasResizeController->setEnabled(hasFiniteDocumentBounds());
    m_canvasResizeController->setCallbacks({ [this](QSize size) { setCanvasSize(size); },
        [this]() { requestRender(); }, [this]() { emit canvasContentChanged(); },
        [this]() { updateToolCursor(); }, [this]() { updateSelectionActionPopup(); },
        [this]() { commitTransformBeforeDocumentMutation(); } });
    // Wire the overlay signal handlers exactly once. They are lambdas, so we
    // cannot rely on Qt::UniqueConnection (it asserts on non-member-function
    // slots in debug builds); guard with a flag since the controller and this
    // panel share the same lifetime.
    if (!m_canvasResizeOverlaySignalsConnected) {
        m_canvasResizeOverlaySignalsConnected = true;
        connect(
            m_canvasResizeController, &CanvasResizeController::overlayStateChanged, this, [this]() {
                m_canvasResizePreviewSize = m_canvasResizeController
                    ? m_canvasResizeController->targetCanvasSize()
                    : m_canvasSize;
                if (m_toolStateOverlay) {
                    m_toolStateOverlay->setCanvasResizeInfo(
                        m_canvasSize, m_canvasResizePreviewSize);
                }
                syncToolStateOverlayContent();
            });
        connect(m_canvasResizeController, &CanvasResizeController::previewSizeChanged, this,
            [this](const QSize& size) {
                m_canvasResizePreviewSize = size.isValid() ? size : m_canvasSize;
                if (m_toolStateOverlay) {
                    m_toolStateOverlay->setCanvasResizeInfo(
                        m_canvasSize, m_canvasResizePreviewSize);
                }
            });
    }
}

void CanvasPanel::setupExportAreaController()
{
    if (!m_exportAreaController || !m_glWidget) {
        return;
    }

    // TRANSITIONAL QUARANTINE: same as the resize controller above.
    m_exportAreaController->setGlWidget(m_glWidget);
    m_exportAreaController->setCanvasSize(m_canvasSize);
    m_exportAreaController->setExportFrame(effectiveDisplayFrame());
}

void CanvasPanel::onRenderSessionReady()
{
    applyZoomLimits();
    publishEffectiveExportFrameIfChanged();
    if (m_engineBinding) {
        auto& view = m_engineBinding->session().view();
        const QRect displayFrame = effectiveDisplayFrame();
        view.centerCameraOn(QPointF(static_cast<qreal>(displayFrame.center().x()) + 0.5,
            static_cast<qreal>(displayFrame.center().y()) + 0.5));

        if (m_playNewProjectAppearanceAnimation
            || m_deferLoadingOverlayHideUntilAppearanceAnimation) {
            // Prepare for zoom-in animation: start at min zoom; animation will smoothly
            // transition to start zoom (minZoom * 3). Do not set start zoom here.
            view.setZoomLimits(0.001, view.maxZoom());
            view.setZoom(0.001);
            emit zoomChanged(view.zoom());
        } else {
            const qreal startZoom
                = std::clamp(view.minZoom() * 3.0, view.minZoom(), view.maxZoom());
            view.setZoom(startZoom);
            emit zoomChanged(view.zoom());
        }
    }
    emit renderContentReady();
    requestRender();
}

} // namespace ruwa::ui::workspace
