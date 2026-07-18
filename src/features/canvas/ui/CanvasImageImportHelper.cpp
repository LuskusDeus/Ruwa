// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   I M A G E   I M P O R T   H E L P E R
// ==========================================================================

#include "CanvasImageImportHelper.h"
#include "CanvasPanel.h"
#include "CanvasPanelHelpers.h"
#include "CanvasCursorManager.h"
#include "ImageImportSelectionOverlay.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/layers/model/LayerData.h"
#include "features/layers/model/LayerModel.h"
#include "shared/undo/LayerAddCommand.h"

#include <QClipboard>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QPixmap>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QtConcurrent>

namespace ruwa::ui::workspace {
namespace {

constexpr qint64 kMaxDroppedBrowserImageBytes = 50 * 1024 * 1024;

QStringList normalizedImportFilePaths(const QStringList& filePaths)
{
    QStringList normalized;
    QSet<QString> seen;

    for (const QString& filePath : filePaths) {
        const QString trimmedPath = filePath.trimmed();
        if (trimmedPath.isEmpty() || seen.contains(trimmedPath)) {
            continue;
        }
        normalized.append(trimmedPath);
        seen.insert(trimmedPath);
    }

    return normalized;
}

detail::ImportedLayerBatch buildImportedLayerBatchForMode(
    const QStringList& filePaths, ImageImportMode mode)
{
    switch (mode) {
    case ImageImportMode::BoardLayer:
        return detail::buildImportedBoardLayerBatch(filePaths);
    case ImageImportMode::SmartLayer:
    default:
        return detail::buildImportedSmartLayerBatch(filePaths);
    }
}

detail::ImportedLayerBatch buildImportedLayerBatchForMode(
    QImage image, const QString& layerName, ImageImportMode mode)
{
    switch (mode) {
    case ImageImportMode::BoardLayer:
        return detail::buildImportedBoardLayerBatchFromImage(std::move(image), layerName);
    case ImageImportMode::SmartLayer:
    default:
        return detail::buildImportedSmartLayerBatchFromImage(std::move(image), layerName);
    }
}

QList<std::shared_ptr<ruwa::core::layers::LayerData>> materializeAndPlaceImportedLayers(
    QList<detail::ImportedLayerPayload> payloads, CanvasPanel* panel)
{
    auto layers = detail::materializeImportedLayers(std::move(payloads));
    if (!panel || layers.isEmpty()) {
        return layers;
    }

    detail::placeImportedSmartLayers(
        layers, panel->effectiveDisplayFrame(), !panel->infiniteCanvasEnabled());
    return layers;
}

} // namespace

CanvasImageImportHelper::CanvasImageImportHelper(CanvasPanel* panel)
    : m_panel(panel)
{
}

bool CanvasImageImportHelper::canImportImageFromMime(const QMimeData* mimeData) const
{
    return detail::mayContainImportableImageFromMime(mimeData);
}

QStringList CanvasImageImportHelper::extractImportableImagePaths(const QMimeData* mimeData) const
{
    return detail::extractImportableImagePathsFromMime(mimeData);
}

bool CanvasImageImportHelper::importImageFromMime(const QMimeData* mimeData)
{
    if (!m_panel || !m_panel->m_layerModel || !mimeData) {
        return false;
    }

    const QStringList filePaths = detail::extractImportableImagePathsFromMime(mimeData);
    if (!filePaths.isEmpty()) {
        promptImportImageFiles(filePaths);
        return true;
    }

    detail::ImportableMimeImage directImage
        = detail::extractImageFromMime(mimeData, QObject::tr("Dropped image"));
    if (directImage.isValid()) {
        promptImportImage(directImage.image, directImage.layerName);
        return true;
    }

    const QList<QUrl> remoteUrls = detail::extractRemoteImageUrlsFromMime(mimeData);
    if (!remoteUrls.isEmpty()) {
        downloadAndPromptRemoteImage(remoteUrls.first());
        return true;
    }

    return false;
}

void CanvasImageImportHelper::applyImportedBatchAtRoot(detail::ImportedLayerBatch batch)
{
    if (!m_panel || !m_panel->m_layerModel || batch.isEmpty()) {
        return;
    }

    m_panel->commitTransformBeforeDocumentMutation();
    if (m_panel->m_glWidget) {
        m_panel->m_glWidget->clearSelectionMask();
    }
    m_panel->updateSelectionActionPopup();

    auto importedLayers = materializeAndPlaceImportedLayers(std::move(batch.layers), m_panel);
    auto undoLayers = materializeAndPlaceImportedLayers(std::move(batch.undoLayers), m_panel);
    if (importedLayers.isEmpty()) {
        return;
    }

    QList<std::pair<ruwa::core::layers::LayerId, int>> positions;
    positions.reserve(undoLayers.size());
    for (int i = 0; i < undoLayers.size(); ++i) {
        positions.append({ ruwa::core::layers::LayerId(), i });
    }

    const ruwa::core::layers::LayerId lastImportedLayerId = importedLayers.constLast()->id;
    m_panel->m_layerModel->addLayers(importedLayers, 0);
    if (!lastImportedLayerId.isNull()) {
        m_panel->m_layerModel->setSelectedLayer(lastImportedLayerId);
    }

    if (auto* undoManager = m_panel->undoManagerOrNull(); !undoLayers.isEmpty() && undoManager) {
        auto requestRender = [panel = m_panel]() {
            if (panel)
                panel->requestRender();
        };
        auto onContentChanged = [panel = m_panel]() {
            if (panel)
                panel->notifyContentChanged();
        };
        auto cmd = std::make_unique<aether::LayerAddCommand>(m_panel->m_layerModel,
            std::move(undoLayers), std::move(positions), requestRender, onContentChanged);
        undoManager->push(std::move(cmd));
    }
}

void CanvasImageImportHelper::applyImportedBatchBelowSelectedKeepingSelection(
    detail::ImportedLayerBatch batch, const QUuid& anchorLayerId)
{
    if (!m_panel || !m_panel->m_layerModel || batch.isEmpty()) {
        return;
    }

    m_panel->commitTransformBeforeDocumentMutation();
    if (m_panel->m_glWidget) {
        m_panel->m_glWidget->clearSelectionMask();
    }
    m_panel->updateSelectionActionPopup();

    auto importedLayers = materializeAndPlaceImportedLayers(std::move(batch.layers), m_panel);
    auto undoLayers = materializeAndPlaceImportedLayers(std::move(batch.undoLayers), m_panel);
    if (importedLayers.isEmpty()) {
        return;
    }

    ruwa::core::layers::LayerData* anchorLayer = m_panel->m_layerModel->layerById(anchorLayerId);
    const ruwa::core::layers::LayerId lastImportedLayerId = importedLayers.constLast()->id;
    QList<std::pair<ruwa::core::layers::LayerId, int>> positions;
    positions.reserve(undoLayers.size());

    if (!anchorLayer || anchorLayer->isBackground()) {
        for (int i = 0; i < undoLayers.size(); ++i) {
            positions.append({ ruwa::core::layers::LayerId(), i });
        }
        m_panel->m_layerModel->addLayers(importedLayers, 0);
        if (!lastImportedLayerId.isNull()) {
            m_panel->m_layerModel->setSelectedLayer(lastImportedLayerId);
        }
    } else if (!anchorLayer->parent) {
        int anchorRootIndex = -1;
        const auto& roots = m_panel->m_layerModel->rootLayers();
        for (int i = 0; i < roots.size(); ++i) {
            if (roots[i].get() == anchorLayer) {
                anchorRootIndex = i;
                break;
            }
        }

        const int insertRootIndex = anchorRootIndex >= 0 ? anchorRootIndex + 1 : 0;
        for (int i = 0; i < undoLayers.size(); ++i) {
            positions.append({ ruwa::core::layers::LayerId(), insertRootIndex + i });
        }
        m_panel->m_layerModel->addLayers(importedLayers, insertRootIndex);
        m_panel->m_layerModel->setSelectedLayer(anchorLayerId);
    } else {
        int insertChildIndex = qMax(0, anchorLayer->indexInParent() + 1);
        for (int i = 0; i < undoLayers.size(); ++i) {
            positions.append({ anchorLayer->parent->id, insertChildIndex + i });
        }
        for (const auto& layer : importedLayers) {
            m_panel->m_layerModel->addLayerTo(layer, anchorLayer->parent, insertChildIndex++);
        }
        m_panel->m_layerModel->setSelectedLayer(anchorLayerId);
    }

    if (auto* undoManager = m_panel->undoManagerOrNull(); !undoLayers.isEmpty() && undoManager) {
        auto requestRender = [panel = m_panel]() {
            if (panel)
                panel->requestRender();
        };
        auto onContentChanged = [panel = m_panel]() {
            if (panel)
                panel->notifyContentChanged();
        };
        auto cmd = std::make_unique<aether::LayerAddCommand>(m_panel->m_layerModel,
            std::move(undoLayers), std::move(positions), requestRender, onContentChanged);
        undoManager->push(std::move(cmd));
    }
}

void CanvasImageImportHelper::promptImportImageFiles(const QStringList& filePaths)
{
    if (!m_panel) {
        return;
    }

    const QStringList normalizedPaths = normalizedImportFilePaths(filePaths);
    if (normalizedPaths.isEmpty()) {
        return;
    }

    ensureImportSelectionOverlay();
    if (!m_panel->m_imageImportSelectionOverlay) {
        importImageFiles(normalizedPaths);
        return;
    }

    m_panel->m_imageImportSelectionOverlay->showForFiles(normalizedPaths);
}

void CanvasImageImportHelper::importImageFiles(const QStringList& filePaths)
{
    importImageFilesAsMode(filePaths, ImageImportMode::SmartLayer);
}

void CanvasImageImportHelper::importImageFilesAsMode(
    const QStringList& filePaths, ImageImportMode mode)
{
    const QStringList normalizedPaths = normalizedImportFilePaths(filePaths);
    if (!m_panel->m_layerModel || normalizedPaths.isEmpty()) {
        return;
    }

    auto* watcher = new QFutureWatcher<detail::ImportedLayerBatch>(m_panel);
    QPointer<CanvasPanel> panel = m_panel;

    QObject::connect(watcher, &QFutureWatcher<detail::ImportedLayerBatch>::finished, m_panel,
        [this, watcher, panel]() {
            watcher->deleteLater();
            if (!panel || !panel->m_layerModel) {
                return;
            }

            applyImportedBatchAtRoot(watcher->result());
        });

    watcher->setFuture(QtConcurrent::run([normalizedPaths, mode]() {
        return buildImportedLayerBatchForMode(normalizedPaths, mode);
    }));
}

void CanvasImageImportHelper::importImageFilesBelowSelectedKeepingSelection(
    const QStringList& filePaths)
{
    const QStringList normalizedPaths = normalizedImportFilePaths(filePaths);
    if (!m_panel->m_layerModel || normalizedPaths.isEmpty()) {
        return;
    }

    const ruwa::core::layers::LayerId anchorLayerId = m_panel->m_layerModel->selectedLayerId();

    auto* watcher = new QFutureWatcher<detail::ImportedLayerBatch>(m_panel);
    QPointer<CanvasPanel> panel = m_panel;

    QObject::connect(watcher, &QFutureWatcher<detail::ImportedLayerBatch>::finished, m_panel,
        [this, watcher, panel, anchorLayerId]() {
            watcher->deleteLater();
            if (!panel || !panel->m_layerModel) {
                return;
            }

            applyImportedBatchBelowSelectedKeepingSelection(watcher->result(), anchorLayerId);
        });

    watcher->setFuture(QtConcurrent::run(
        [normalizedPaths]() { return detail::buildImportedSmartLayerBatch(normalizedPaths); }));
}

void CanvasImageImportHelper::importImageBelowSelectedKeepingSelection(
    const QImage& image, const QString& layerName)
{
    if (!m_panel->m_layerModel || image.isNull()) {
        return;
    }

    const QString importLayerName
        = layerName.trimmed().isEmpty() ? QObject::tr("Dropped image") : layerName.trimmed();
    const ruwa::core::layers::LayerId anchorLayerId = m_panel->m_layerModel->selectedLayerId();
    auto* watcher = new QFutureWatcher<detail::ImportedLayerBatch>(m_panel);
    QPointer<CanvasPanel> panel = m_panel;

    QObject::connect(watcher, &QFutureWatcher<detail::ImportedLayerBatch>::finished, m_panel,
        [this, watcher, panel, anchorLayerId]() {
            watcher->deleteLater();
            if (!panel || !panel->m_layerModel) {
                return;
            }

            applyImportedBatchBelowSelectedKeepingSelection(watcher->result(), anchorLayerId);
        });

    watcher->setFuture(QtConcurrent::run([image, importLayerName]() mutable {
        return detail::buildImportedSmartLayerBatchFromImage(std::move(image), importLayerName);
    }));
}

void CanvasImageImportHelper::promptImportImage(const QImage& image, const QString& layerName)
{
    if (!m_panel || image.isNull()) {
        return;
    }

    ensureImportSelectionOverlay();
    if (!m_panel->m_imageImportSelectionOverlay) {
        importImageAsMode(image, layerName, ImageImportMode::SmartLayer);
        return;
    }

    m_pendingOverlayImageLayerName
        = layerName.trimmed().isEmpty() ? QObject::tr("Dropped image") : layerName.trimmed();
    m_panel->m_imageImportSelectionOverlay->showForSingleImage(image, QObject::tr("Import image"));
}

void CanvasImageImportHelper::importImageAsMode(
    const QImage& image, const QString& layerName, ImageImportMode mode)
{
    if (!m_panel->m_layerModel || image.isNull()) {
        return;
    }

    const QString importLayerName
        = layerName.trimmed().isEmpty() ? QObject::tr("Dropped image") : layerName.trimmed();
    auto* watcher = new QFutureWatcher<detail::ImportedLayerBatch>(m_panel);
    QPointer<CanvasPanel> panel = m_panel;

    QObject::connect(watcher, &QFutureWatcher<detail::ImportedLayerBatch>::finished, m_panel,
        [this, watcher, panel]() {
            watcher->deleteLater();
            if (!panel || !panel->m_layerModel) {
                return;
            }

            applyImportedBatchAtRoot(watcher->result());
        });

    watcher->setFuture(QtConcurrent::run([image, importLayerName, mode]() mutable {
        return buildImportedLayerBatchForMode(std::move(image), importLayerName, mode);
    }));
}

QNetworkAccessManager* CanvasImageImportHelper::networkManager()
{
    if (!m_panel) {
        return nullptr;
    }
    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager(m_panel);
    }
    return m_networkManager;
}

void CanvasImageImportHelper::downloadAndPromptRemoteImage(const QUrl& url)
{
    QNetworkAccessManager* manager = networkManager();
    if (!manager || !url.isValid()) {
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(
        QNetworkRequest::UserAgentHeader, QStringLiteral("Ruwa image drag-drop import"));

    const quint64 generation = ++m_remoteImageLoadGeneration;
    QNetworkReply* reply = manager->get(request);
    QPointer<CanvasPanel> panel = m_panel;

    QObject::connect(
        reply, &QNetworkReply::downloadProgress, m_panel, [reply](qint64 received, qint64) {
            if (received > kMaxDroppedBrowserImageBytes) {
                reply->abort();
            }
        });
    QObject::connect(
        reply, &QNetworkReply::finished, m_panel, [this, reply, panel, generation, url]() {
            reply->deleteLater();
            if (!panel || m_remoteImageLoadGeneration != generation) {
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                return;
            }

            const QByteArray data = reply->readAll();
            if (data.size() > kMaxDroppedBrowserImageBytes) {
                return;
            }

            QImage image = detail::decodeImageData(data);
            if (image.isNull()) {
                return;
            }

            promptImportImage(image, detail::suggestedLayerNameForImageUrl(url));
        });
}

bool CanvasImageImportHelper::importImageFromClipboard()
{
    if (!m_panel->m_layerModel) {
        return false;
    }

    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* mimeData = clipboard ? clipboard->mimeData() : nullptr;
    if (!mimeData) {
        return false;
    }

    if (mimeData->hasImage() || mimeData->hasFormat(QStringLiteral("image/png"))) {
        if (mimeData->hasFormat(QStringLiteral("image/png"))) {
            const QByteArray pngData = mimeData->data(QStringLiteral("image/png"));
            if (!pngData.isEmpty()) {
                m_pendingOverlayImageLayerName = QObject::tr("Pasted image");
                ensureImportSelectionOverlay();
                if (m_panel->m_imageImportSelectionOverlay) {
                    m_panel->m_imageImportSelectionOverlay->showForClipboardImage(QImage());
                }

                const quint64 generation = ++m_clipboardLoadGeneration;
                auto* watcher = new QFutureWatcher<QImage>(m_panel);
                QPointer<CanvasPanel> panel = m_panel;

                QObject::connect(watcher, &QFutureWatcher<QImage>::finished, m_panel,
                    [this, watcher, panel, generation]() {
                        watcher->deleteLater();
                        if (!panel || m_clipboardLoadGeneration != generation) {
                            return;
                        }

                        const QImage image = watcher->result();
                        if (image.isNull()) {
                            if (panel->m_imageImportSelectionOverlay) {
                                panel->m_imageImportSelectionOverlay->hideOverlay();
                            }
                            return;
                        }

                        if (panel->m_imageImportSelectionOverlay) {
                            panel->m_imageImportSelectionOverlay->showForClipboardImage(image);
                        } else {
                            importImageAsMode(
                                image, QObject::tr("Pasted image"), ImageImportMode::SmartLayer);
                        }
                    });

                watcher->setFuture(QtConcurrent::run([pngData]() {
                    QImage image;
                    image.loadFromData(pngData, "PNG");
                    return image;
                }));
                return true;
            }
        }

        QImage image;
        if (mimeData->hasImage()) {
            image = qvariant_cast<QImage>(mimeData->imageData());
        }
        if (image.isNull()) {
            const QPixmap pixmap = qvariant_cast<QPixmap>(mimeData->imageData());
            image = pixmap.toImage();
        }

        if (!image.isNull()) {
            ++m_clipboardLoadGeneration;
            m_pendingOverlayImageLayerName = QObject::tr("Pasted image");
            ensureImportSelectionOverlay();
            if (m_panel->m_imageImportSelectionOverlay) {
                m_panel->m_imageImportSelectionOverlay->showForClipboardImage(image);
                return true;
            }
            importImageAsMode(image, QObject::tr("Pasted image"), ImageImportMode::SmartLayer);
            return true;
        }
    } else {
        const QStringList filePaths = detail::extractImportableImagePathsFromMime(mimeData);
        if (!filePaths.isEmpty()) {
            promptImportImageFiles(filePaths);
            return true;
        }
    }

    return false;
}

void CanvasImageImportHelper::ensureImportSelectionOverlay()
{
    if (!m_panel || m_panel->m_imageImportSelectionOverlay || !m_panel->m_contentWidget) {
        return;
    }

    m_panel->m_imageImportSelectionOverlay
        = new ImageImportSelectionOverlay(m_panel->m_contentWidget);
    m_panel->m_imageImportSelectionOverlay->hide();
    m_panel->m_imageImportSelectionOverlay->raise();

    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->addCursorExclusionWidget(m_panel->m_imageImportSelectionOverlay);
    }

    QObject::connect(m_panel->m_imageImportSelectionOverlay,
        &ImageImportSelectionOverlay::importRequested, m_panel,
        [this](const QStringList& selectedFilePaths, ImageImportMode mode) {
            importImageFilesAsMode(selectedFilePaths, mode);
        });
    QObject::connect(m_panel->m_imageImportSelectionOverlay,
        &ImageImportSelectionOverlay::singleImageImportRequested, m_panel,
        [this](const QImage& image, ImageImportMode mode) {
            importImageAsMode(image, m_pendingOverlayImageLayerName, mode);
        });
}

} // namespace ruwa::ui::workspace
