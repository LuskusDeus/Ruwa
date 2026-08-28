// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_DOCUMENT_CANVASDOCUMENTFACADE_H
#define RUWA_FEATURES_CANVAS_DOCUMENT_CANVASDOCUMENTFACADE_H

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QUuid>

#include <memory>
#include <optional>

namespace aether {
class TileGrid;
} // namespace aether

namespace ruwa::core::layers {
struct SmartDocument;
} // namespace ruwa::core::layers

namespace ruwa::ui::workspace {

/// TRANSITIONAL (plan 7.30.2): application-owned document-action facade.
///
/// Task-oriented document operations that currently happen to be implemented
/// inside the legacy renderer widget. They are deliberately NOT
/// CanvasEngineSession capabilities: their final owner (document model, command
/// layer, engine editing service, or another subsystem) is not decided by
/// Stage 1, and the facade keeps that question out of the renderer contract.
/// Exposed by CanvasEngineQtBinding::document(). The Stage 1 implementation
/// forwards to the existing Aether editing code.
class CanvasDocumentFacade {
public:
    virtual ~CanvasDocumentFacade() = default;

    // --- canvas resize (plan 7.30.2: request, not command construction) ---
    struct ResizeRequest {
        QSize oldSize;
        QSize newSize;
        int offsetX = 0;
        int offsetY = 0;
    };

    // --- layer/document mutations (quarantined ownership) ---
    virtual bool clearLayerPixelContent(const QUuid& layerId) = 0;
    virtual bool rasterizeSmartLayer(const QUuid& layerId) = 0;
    virtual bool convertLayerToSmartObject(const QUuid& layerId) = 0;
    virtual bool replaceSmartLayerContents(const QUuid& layerId,
        std::unique_ptr<aether::TileGrid> contentGrid, const QString& sourcePath,
        const QByteArray& sourceHash)
        = 0;
    virtual bool applySmartContentDocument(
        const QUuid& contentId, std::shared_ptr<ruwa::core::layers::SmartDocument> document)
        = 0;
    virtual bool applyLayerMask(const QUuid& layerId) = 0;
    virtual bool invertLayerMask(const QUuid& layerId) = 0;
    virtual bool applyLayerEffects(const QUuid& layerId) = 0;
    virtual bool fillLayerMaskFromActiveSelection(const QUuid& layerId) = 0;

    // --- clipboard pixel transfer (plan 7.25.2 warning: extraction and
    // clipboard policy are temporarily combined here, not on CanvasSelection) ---
    virtual bool copySelectionPixelsToClipboard(QImage* outFlattenedImage = nullptr) = 0;
    virtual bool copyMergedSelectionPixelsToClipboard(QImage* outFlattenedImage = nullptr) = 0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_FEATURES_CANVAS_DOCUMENT_CANVASDOCUMENTFACADE_H
