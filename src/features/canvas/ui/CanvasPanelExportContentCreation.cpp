// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   E X P O R T   C O N T E N T
// ==========================================================================

#include "CanvasPanel.h"

#include "features/export/ExportAreaController.h"
#include "features/export/ExportModeController.h"
#include "features/export/ExportSettingsPanel.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/export/ExportSettings.h"

#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace ruwa::ui::workspace {

void CanvasPanel::createExportModeContent()
{
    // Export settings panel (overlay inside content, managed by ExportModeController)
    m_exportPanel = new ExportSettingsPanel(m_contentWidget);
    m_exportPanel->setVisible(false);
    m_exportPanel->raise();
    m_exportController = new ExportModeController(m_contentWidget, this, m_exportPanel, this);
    m_exportAreaController = new ExportAreaController(this);
    m_exportAreaController->setContentWidget(m_contentWidget);
    m_exportAreaController->setCanvasSize(m_canvasSize);
    m_exportAreaController->setCanvasBoundsMode(m_canvasBoundsMode);
    m_exportAreaController->setExportFrame(effectiveDisplayFrame());
    m_exportAreaController->setCallbacks(
        { [this](const QRect& frame) { setExportFrame(frame); }, [this]() { requestRender(); } });

    // Keep the panel's frame fields in sync with the canvas handles.
    m_exportPanel->setExportFrame(effectiveDisplayFrame());
    m_exportPanel->setDefaultExportFrame(defaultExportFrame());

    // The size estimate is measured from a small render of the frame's
    // content. Frame drags fire per pointer step, so re-sampling is debounced;
    // the panel debounces the encode on top of this.
    auto* sampleDebounce = new QTimer(this);
    sampleDebounce->setSingleShot(true);
    sampleDebounce->setInterval(200);
    connect(sampleDebounce, &QTimer::timeout, this, &CanvasPanel::refreshExportPanelSample);
    connect(this, &CanvasPanel::exportFrameChanged, this,
        [this, sampleDebounce]() { sampleDebounce->start(); });

    // The width/height fields stop at the canvas edge. An infinite canvas has
    // no edge, so it gets no limit rather than an arbitrary one.
    const auto pushFrameSizeLimit = [this]() {
        m_exportPanel->setFrameSizeLimit(hasFiniteDocumentBounds() ? m_canvasSize : QSize());
    };
    pushFrameSizeLimit();
    connect(this, &CanvasPanel::canvasSizeChanged, m_exportPanel,
        [this, pushFrameSizeLimit](const QSize&) { pushFrameSizeLimit(); });
    connect(this, &CanvasPanel::canvasBoundsModeChanged, m_exportPanel,
        [this, pushFrameSizeLimit](ruwa::core::canvas::CanvasBoundsMode) { pushFrameSizeLimit(); });
    connect(this, &CanvasPanel::exportFrameChanged, m_exportPanel,
        &ExportSettingsPanel::setExportFrame);
    connect(this, &CanvasPanel::exportFrameChanged, this, [this](const QRect& frame) {
        if (m_exportAreaController) {
            m_exportAreaController->setExportFrame(frame);
        }
    });
    connect(m_exportController, &ExportModeController::exportModeChanged, this,
        [this, sampleDebounce](bool active) {
            if (!m_exportAreaController) {
                return;
            }
            if (active) {
                if (isInfiniteCanvas()) {
                    m_exportAreaController->setExportFrame(effectiveDisplayFrame());
                }
                if (m_exportPanel) {
                    // The content bounds of an infinite canvas move as the user
                    // paints, so what Reset restores is only knowable now.
                    m_exportPanel->setDefaultExportFrame(defaultExportFrame());
                    m_exportPanel->setFrameSizeLimit(
                        hasFiniteDocumentBounds() ? m_canvasSize : QSize());
                }
                sampleDebounce->stop();
                refreshExportPanelSample();
                m_exportAreaController->enter();
                updateExportAreaCursor();
            } else {
                m_exportAreaController->exit();
                if (m_glWidget) {
                    m_glWidget->unsetCursor();
                }
                if (m_contentWidget) {
                    m_contentWidget->unsetCursor();
                }
                unsetCursor();
            }
        });

    connect(m_exportPanel, &ExportSettingsPanel::colorPickerRequested, this,
        &CanvasPanel::colorPickerRequested);

    // Exit button → leave export mode
    connect(m_exportPanel, &ExportSettingsPanel::exitRequested, m_exportController,
        &ExportModeController::exit);

    // Width / height fields -> resize the frame, then let the change come back
    // through exportFrameChanged. The panel never writes the frame itself, so
    // the fields and the on-canvas handles cannot drift apart.
    connect(m_exportPanel, &ExportSettingsPanel::exportFrameResizeRequested, this,
        &CanvasPanel::resizeExportFrame);
    connect(m_exportPanel, &ExportSettingsPanel::exportFrameResetRequested, this,
        &CanvasPanel::resetExportFrameToDefault);

    // Export button -> the panel hands over a finished settings object and the
    // service does the rest. No dialog: the destination is a field in the panel.
    connect(m_exportPanel, &ExportSettingsPanel::exportRequested, this,
        [this](const ruwa::core::exporting::ExportSettings& settings) {
            ruwa::core::exporting::ExportSettings request = settings;
            startExport(request);
        });
}

} // namespace ruwa::ui::workspace
