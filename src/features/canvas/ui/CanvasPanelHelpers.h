// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   H E L P E R S
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_PANELS_CANVASPANELHELPERS_H
#define RUWA_UI_WORKSPACE_PANELS_CANVASPANELHELPERS_H

#include <QColor>
#include <QByteArray>
#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUuid>
#include <QVector>

class QMimeData;

#include <Qt>
#include <memory>
#include <utility>

#include "shared/types/Types.h"
#include "features/transform/TransformState.h"

namespace aether {
class Canvas;
class TileGrid;
} // namespace aether

namespace ruwa::core::layers {
struct LayerData;
class LayerModel;
enum class LayerType;
} // namespace ruwa::core::layers

namespace ruwa::ui::workspace::detail {

struct ImportedLayerPayload {
    QUuid id;
    QString layerName;
    ruwa::core::layers::LayerType layerType;
    aether::Rect contentBounds;
    std::shared_ptr<aether::TileGrid> pixelGrid;

    bool isValid() const { return !id.isNull() && pixelGrid != nullptr; }
};

struct ImportedLayerBatch {
    QList<ImportedLayerPayload> layers;
    QList<ImportedLayerPayload> undoLayers;

    bool isEmpty() const { return layers.isEmpty(); }
};

struct ImportableMimeImage {
    QImage image;
    QString layerName;

    bool isValid() const { return !image.isNull(); }
};

QColor sampleColorFromActiveLayer(
    const ruwa::core::layers::LayerModel* layerModel, const aether::Canvas& canvas, int x, int y);
QColor sampleColorFromLayerModel(
    const ruwa::core::layers::LayerModel* layerModel, const aether::Canvas& canvas, int x, int y);

Qt::CursorShape cursorForTransformHandle(
    const aether::TransformHitResult& hit, bool cornersActAsRotationHandles = false);
Qt::CursorShape cursorForTransformHandle(
    aether::TransformHandle handle, bool cornersActAsRotationHandles = false);

float normalizeAngleDelta(float delta);
bool isAngleEffectivelyZero(float radians);

std::shared_ptr<ruwa::core::layers::LayerData> buildSmartLayerFromImage(
    QImage image, const QString& layerName);
std::shared_ptr<ruwa::core::layers::LayerData> buildBoardLayerFromImage(
    QImage image, const QString& layerName);
QList<std::shared_ptr<ruwa::core::layers::LayerData>> buildImportedSmartLayers(
    const QStringList& filePaths);
QList<std::shared_ptr<ruwa::core::layers::LayerData>> buildImportedBoardLayers(
    const QStringList& filePaths);
ImportedLayerBatch buildImportedSmartLayerBatch(const QStringList& filePaths);
ImportedLayerBatch buildImportedBoardLayerBatch(const QStringList& filePaths);
ImportedLayerBatch buildImportedRasterLayerBatch(const QStringList& filePaths);
ImportedLayerBatch buildImportedSmartLayerBatchFromImage(QImage image, const QString& layerName);
ImportedLayerBatch buildImportedBoardLayerBatchFromImage(QImage image, const QString& layerName);
ImportedLayerBatch buildImportedRasterLayerBatchFromImage(QImage image, const QString& layerName);
QList<std::shared_ptr<ruwa::core::layers::LayerData>> materializeImportedLayers(
    QList<ImportedLayerPayload> payloads);

/// Position imported isolated pixel layers relative to a display/export frame.
/// When @p fitToFrame is true they are uniformly downscaled to fit, otherwise only centered.
void placeImportedSmartLayers(QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers,
    const QRect& frame, bool fitToFrame);

/// Extract image payloads from MIME data (e.g. drag-drop, clipboard, browser drops).
bool mayContainImportableImageFromMime(const QMimeData* mimeData);
ImportableMimeImage extractImageFromMime(
    const QMimeData* mimeData, const QString& fallbackLayerName);
QList<QUrl> extractRemoteImageUrlsFromMime(const QMimeData* mimeData);
QString suggestedLayerNameForImageUrl(const QUrl& url);
QImage decodeImageData(const QByteArray& data, const QByteArray& formatHint = {});

/// Extract local file paths of importable images from MIME data.
bool mayContainImportableImagePathsFromMime(const QMimeData* mimeData);
QStringList extractImportableImagePathsFromMime(const QMimeData* mimeData);

} // namespace ruwa::ui::workspace::detail

#endif // RUWA_UI_WORKSPACE_PANELS_CANVASPANELHELPERS_H
