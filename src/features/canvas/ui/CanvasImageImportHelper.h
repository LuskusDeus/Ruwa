// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   I M A G E   I M P O R T   H E L P E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASIMAGEIMPORTHELPER_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASIMAGEIMPORTHELPER_H

#include <QStringList>
#include <QtGlobal>
#include <memory>

class QImage;
class QMimeData;
class QNetworkAccessManager;
class QUrl;
class QUuid;

namespace ruwa::core::layers {
struct LayerData;
}

namespace ruwa::ui::workspace {

class CanvasPanel;
enum class ImageImportMode : int;
namespace detail {
struct ImportedLayerBatch;
}

/**
 * @brief Handles image import (drag-drop, clipboard) for CanvasPanel.
 *
 * Extracted from CanvasPanel to isolate ~130 lines: importImageFiles,
 * importImageFilesBelowSelectedKeepingSelection, importImageFromClipboard.
 * Uses CanvasPanelHelpers for buildImportedSmartLayers / buildSmartLayerFromImage.
 */
class CanvasImageImportHelper {
public:
    explicit CanvasImageImportHelper(CanvasPanel* panel);

    bool canImportImageFromMime(const QMimeData* mimeData) const;
    QStringList extractImportableImagePaths(const QMimeData* mimeData) const;
    bool importImageFromMime(const QMimeData* mimeData);
    void promptImportImageFiles(const QStringList& filePaths);
    void importImageFiles(const QStringList& filePaths);
    void importImageFilesBelowSelectedKeepingSelection(const QStringList& filePaths);
    void importImageBelowSelectedKeepingSelection(const QImage& image, const QString& layerName);
    bool importImageFromClipboard();

private:
    void applyImportedBatchAtRoot(detail::ImportedLayerBatch batch);
    void applyImportedBatchBelowSelectedKeepingSelection(
        detail::ImportedLayerBatch batch, const QUuid& anchorLayerId);
    void ensureImportSelectionOverlay();
    void promptImportImage(const QImage& image, const QString& layerName);
    void downloadAndPromptRemoteImage(const QUrl& url);
    void importImageFilesAsMode(const QStringList& filePaths, ImageImportMode mode);
    void importImageAsMode(const QImage& image, const QString& layerName, ImageImportMode mode);
    QNetworkAccessManager* networkManager();

    CanvasPanel* m_panel = nullptr;
    quint64 m_clipboardLoadGeneration = 0;
    quint64 m_remoteImageLoadGeneration = 0;
    QString m_pendingOverlayImageLayerName;
    QNetworkAccessManager* m_networkManager = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASIMAGEIMPORTHELPER_H
