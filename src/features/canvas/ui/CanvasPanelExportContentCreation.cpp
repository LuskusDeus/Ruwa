// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   E X P O R T   C O N T E N T
// ==========================================================================

#include "CanvasPanel.h"

#include "features/export/ExportAreaController.h"
#include "features/export/ExportModeController.h"
#include "features/export/ExportSettingsPanel.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "shared/utils/FileDialogMemory.h"

#include <QImage>
#include <QImageWriter>
#include <QRect>
#include <QString>
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

    // Keep panel export-size label in sync
    m_exportPanel->setExportFrame(effectiveDisplayFrame());
    connect(this, &CanvasPanel::exportFrameChanged, m_exportPanel,
        &ExportSettingsPanel::setExportFrame);
    connect(this, &CanvasPanel::exportFrameChanged, this, [this](const QRect& frame) {
        if (m_exportAreaController) {
            m_exportAreaController->setExportFrame(frame);
        }
    });
    connect(
        m_exportController, &ExportModeController::exportModeChanged, this, [this](bool active) {
            if (!m_exportAreaController) {
                return;
            }
            if (active) {
                if (isInfiniteCanvas()) {
                    m_exportAreaController->setExportFrame(effectiveDisplayFrame());
                }
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

    // Exit button → leave export mode
    connect(m_exportPanel, &ExportSettingsPanel::exitRequested, m_exportController,
        &ExportModeController::exit);

    // Export button → save file
    connect(m_exportPanel, &ExportSettingsPanel::exportRequested, this,
        [this](const QString& format, int jpegQuality) {
            const QImage image = exportCanvasImage();
            if (image.isNull())
                return;

            QString filter;
            QString defaultSuffix;
            if (format == "JPEG") {
                filter = tr("JPEG Image (*.jpg *.jpeg)");
                defaultSuffix = "jpg";
            } else if (format == "WEBP") {
                filter = tr("WebP Image (*.webp)");
                defaultSuffix = "webp";
            } else {
                filter = tr("PNG Image (*.png)");
                defaultSuffix = "png";
            }

            QString path = ruwa::shared::filedialog::getSaveFileName(this,
                ruwa::shared::filedialog::category::kCanvasExport, tr("Export Canvas"), QString(),
                filter);
            if (path.isEmpty())
                return;

            // Ensure correct extension
            const QString lc = path.toLower();
            if (!lc.endsWith("." + defaultSuffix)
                && !(defaultSuffix == "jpg" && lc.endsWith(".jpeg"))) {
                path += "." + defaultSuffix;
            }

            QImageWriter writer(path);
            if (format == "JPEG") {
                writer.setQuality(jpegQuality);
            }
            writer.write(image);
        });
}

} // namespace ruwa::ui::workspace
