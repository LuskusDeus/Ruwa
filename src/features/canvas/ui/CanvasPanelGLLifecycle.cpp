// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   G L   L I F E C Y C L E
// ==========================================================================

#include "CanvasPanel.h"

#include "CanvasToolStateOverlay.h"
#include "TextEditingController.h"
#include "features/canvas-resize/CanvasResizeController.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/export/ExportAreaController.h"

#include <algorithm>

namespace ruwa::ui::workspace {

void CanvasPanel::setupCanvasResizeController()
{
    if (!m_canvasResizeController || !m_glWidget) {
        return;
    }
    m_canvasResizeController->setGlWidget(m_glWidget);
    m_canvasResizeController->setLayerModel(m_layerModel);
    m_canvasResizeController->setCanvasSize(m_canvasSize);
    m_canvasResizeController->setEnabled(hasFiniteDocumentBounds());
    m_canvasResizeController->setCallbacks({ [this](QSize size) { setCanvasSize(size); },
        [this]() { requestRender(); }, [this]() { emit canvasContentChanged(); },
        [this]() { updateToolCursor(); }, [this]() { updateSelectionActionPopup(); } });
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

    m_exportAreaController->setGlWidget(m_glWidget);
    m_exportAreaController->setCanvasSize(m_canvasSize);
    m_exportAreaController->setExportFrame(effectiveDisplayFrame());
}

void CanvasPanel::onGLInitialized()
{
    applyZoomLimits();
    publishEffectiveExportFrameIfChanged();
    if (m_glWidget) {
        auto& cam = m_glWidget->viewport().camera();
        const QRect displayFrame = effectiveDisplayFrame();
        cam.centerOn(aether::Vector2 { static_cast<float>(displayFrame.center().x()) + 0.5f,
            static_cast<float>(displayFrame.center().y()) + 0.5f });

        if (m_playNewProjectAppearanceAnimation
            || m_deferLoadingOverlayHideUntilAppearanceAnimation) {
            // Prepare for zoom-in animation: start at min zoom; animation will smoothly
            // transition to start zoom (minZoom * 3). Do not set start zoom here.
            const float maxZoom = cam.maxZoom();
            cam.setZoomLimits(0.001f, maxZoom);
            cam.setZoom(0.001f);
            emit zoomChanged(static_cast<qreal>(cam.zoom()));
        } else {
            const float startZoom = std::clamp(cam.minZoom() * 3.0f, cam.minZoom(), cam.maxZoom());
            cam.setZoom(startZoom);
            emit zoomChanged(static_cast<qreal>(cam.zoom()));
        }
    }
    emit glContentReady();
    requestRender();
}

} // namespace ruwa::ui::workspace
