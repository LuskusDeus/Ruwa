// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   H E L P E R S
// ==========================================================================

#include "CanvasPanelHelpers.h"

#include "features/canvas/scene/Canvas.h"

#include <QMimeData>
#include <QBuffer>
#include <QIODevice>
#include <QSet>
#include <QUrl>
#include <QRegularExpression>
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TilePixelAccess.h"
#include "shared/tiles/TileTypes.h"
#include "features/layers/model/LayerData.h"
#include "features/layers/model/LayerModel.h"

#include <QFileInfo>
#include <QImageReader>
#include <QObject>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace ruwa::ui::workspace::detail {

// ==========================================================================
//   C O L O R   S A M P L I N G
// ==========================================================================

namespace {

struct PremultipliedColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

PremultipliedColor srcOver(const PremultipliedColor& src, const PremultipliedColor& dst)
{
    PremultipliedColor out;
    out.r = src.r + dst.r * (1.0f - src.a);
    out.g = src.g + dst.g * (1.0f - src.a);
    out.b = src.b + dst.b * (1.0f - src.a);
    out.a = src.a + dst.a * (1.0f - src.a);
    return out;
}

PremultipliedColor sampleRasterLayerPixel(const ruwa::core::layers::LayerData* layer, int x, int y)
{
    PremultipliedColor out;
    const auto* pixelGrid = layer ? layer->pixelGrid() : nullptr;
    if (!pixelGrid)
        return out;

    int sampleX = x;
    int sampleY = y;
    if (layer->isIsolatedPixelLayer() && !layer->smartTransform.isIdentity()) {
        aether::TransformState state = layer->smartTransform;
        if (state.contentBounds.width <= 0.0f || state.contentBounds.height <= 0.0f) {
            state.contentBounds = aether::TransformState::computeContentBounds(*pixelGrid);
            state.pivot = state.contentBounds.center();
        }
        const aether::Vector2 src = state.inverseTransformPoint(
            { static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f });
        sampleX = static_cast<int>(std::floor(src.x));
        sampleY = static_cast<int>(std::floor(src.y));
    }

    const int tileX = sampleX / static_cast<int>(aether::TILE_SIZE);
    const int tileY = sampleY / static_cast<int>(aether::TILE_SIZE);
    const int localX = sampleX % static_cast<int>(aether::TILE_SIZE);
    const int localY = sampleY % static_cast<int>(aether::TILE_SIZE);
    const aether::TileKey key { tileX, tileY };

    const aether::TileData* tile = pixelGrid->getTile(key);
    if (!tile)
        return out;

    // Format-aware read: content grids may be RGBA8/16F/32F under the
    // per-document format model, and the accessor also resolves solid tiles.
    // The returned values are already normalized premultiplied floats.
    float rgba[4];
    aether::readTilePixelF(
        *tile, static_cast<uint32_t>(localX), static_cast<uint32_t>(localY), rgba);
    out.r = rgba[0];
    out.g = rgba[1];
    out.b = rgba[2];
    out.a = rgba[3];
    return out;
}

PremultipliedColor sampleLayerStackAt(
    const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers, int x, int y,
    float inheritedOpacity)
{
    PremultipliedColor result;
    for (int i = layers.size() - 1; i >= 0; --i) {
        const auto& layerPtr = layers[i];
        const auto* layer = layerPtr.get();
        if (!layer || !layer->visible)
            continue;

        const float layerOpacity
            = qBound(0.0f, static_cast<float>(layer->opacity), 1.0f) * inheritedOpacity;
        if (layerOpacity <= 0.0f)
            continue;

        PremultipliedColor src;
        if (layer->isGroup()) {
            src = sampleLayerStackAt(layer->children, x, y, layerOpacity);
        } else if (layer->isPixelLayer()) {
            src = sampleRasterLayerPixel(layer, x, y);
            src.r *= layerOpacity;
            src.g *= layerOpacity;
            src.b *= layerOpacity;
            src.a *= layerOpacity;
        } else {
            continue;
        }

        result = srcOver(src, result);
    }
    return result;
}

const QSet<QString>& supportedImageSuffixes()
{
    static const QSet<QString> suffixes = []() {
        QSet<QString> result;
        const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        for (const QByteArray& format : formats) {
            const QString suffix = QString::fromLatin1(format).trimmed().toLower();
            if (!suffix.isEmpty()) {
                result.insert(suffix);
            }
        }
        return result;
    }();
    return suffixes;
}

QString imageFormatFromMimeFormat(const QString& mimeFormat)
{
    static const QString kPrefix = QStringLiteral("image/");
    if (!mimeFormat.startsWith(kPrefix, Qt::CaseInsensitive)) {
        return {};
    }

    QString format = mimeFormat.mid(kPrefix.size()).trimmed().toLower();
    const int parameterIndex = format.indexOf(QLatin1Char(';'));
    if (parameterIndex >= 0) {
        format.truncate(parameterIndex);
    }
    if (format == QStringLiteral("jpg")) {
        format = QStringLiteral("jpeg");
    } else if (format == QStringLiteral("svg+xml")) {
        format = QStringLiteral("svg");
    } else if (format == QStringLiteral("x-icon")) {
        format = QStringLiteral("ico");
    }
    return format;
}

QImage decodeImageDataInternal(const QByteArray& data, const QByteArray& formatHint)
{
    if (data.isEmpty()) {
        return {};
    }

    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QImageReader reader(&buffer, formatHint);
    reader.setAutoTransform(true);
    return reader.read();
}

QImage extractImageVariant(const QMimeData* mimeData)
{
    if (!mimeData || !mimeData->hasImage()) {
        return {};
    }

    QImage image = qvariant_cast<QImage>(mimeData->imageData());
    if (!image.isNull()) {
        return image;
    }

    const QPixmap pixmap = qvariant_cast<QPixmap>(mimeData->imageData());
    return pixmap.toImage();
}

QImage decodeDataImageUrl(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive)) {
        return {};
    }

    const int commaIndex = trimmed.indexOf(QLatin1Char(','));
    if (commaIndex <= 0) {
        return {};
    }

    const QString metadata = trimmed.mid(5, commaIndex - 5);
    const QString payload = trimmed.mid(commaIndex + 1);
    const QByteArray formatHint
        = imageFormatFromMimeFormat(metadata.section(QLatin1Char(';'), 0, 0)).toLatin1();
    const bool isBase64 = metadata.contains(QStringLiteral(";base64"), Qt::CaseInsensitive);
    const QByteArray bytes = isBase64 ? QByteArray::fromBase64(payload.toLatin1())
                                      : QByteArray::fromPercentEncoding(payload.toLatin1());
    return decodeImageDataInternal(bytes, formatHint);
}

bool isDataImageUrl(const QString& text)
{
    return text.trimmed().startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive);
}

QString firstImageSrcFromHtml(const QString& html)
{
    if (html.isEmpty()) {
        return {};
    }

    static const QRegularExpression imgSrcPattern(
        QStringLiteral("<img\\b[^>]*\\bsrc\\s*=\\s*([\"'])(.*?)\\1"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = imgSrcPattern.match(html);
    return match.hasMatch() ? match.captured(2).trimmed() : QString();
}

QUrl normalizedImageUrl(const QUrl& url)
{
    if (!url.isValid()) {
        return {};
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return {};
    }
    return url;
}

void appendUniqueRemoteImageUrl(QList<QUrl>& urls, QSet<QString>& seen, const QUrl& url)
{
    const QUrl normalized = normalizedImageUrl(url);
    if (!normalized.isValid()) {
        return;
    }

    const QString key = normalized.toString(QUrl::FullyEncoded);
    if (seen.contains(key)) {
        return;
    }

    seen.insert(key);
    urls.append(normalized);
}

QList<QUrl> extractImageUrlsFromHtml(const QString& html)
{
    QList<QUrl> urls;
    QSet<QString> seen;
    if (html.isEmpty()) {
        return urls;
    }

    static const QRegularExpression imgSrcPattern(
        QStringLiteral("<img\\b[^>]*\\bsrc\\s*=\\s*([\"'])(.*?)\\1"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = imgSrcPattern.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        appendUniqueRemoteImageUrl(urls, seen, QUrl::fromUserInput(match.captured(2).trimmed()));
    }

    return urls;
}

ImportableMimeImage makeImportableMimeImage(QImage image, const QString& fallbackLayerName)
{
    ImportableMimeImage result;
    if (image.isNull()) {
        return result;
    }

    result.image = std::move(image);
    result.layerName = fallbackLayerName.trimmed().isEmpty() ? QObject::tr("Dropped image")
                                                             : fallbackLayerName.trimmed();
    return result;
}

} // namespace

QColor sampleColorFromActiveLayer(
    const ruwa::core::layers::LayerModel* layerModel, const aether::Canvas& canvas, int x, int y)
{
    if (!layerModel || x < 0 || y < 0 || x >= static_cast<int>(canvas.width())
        || y >= static_cast<int>(canvas.height())) {
        return QColor(0, 0, 0, 0);
    }

    ruwa::core::layers::LayerData* layer = layerModel->selectedLayer();
    if (!layer || !layer->visible)
        return QColor(0, 0, 0, 0);

    PremultipliedColor premult;
    const float layerOpacity = qBound(0.0f, static_cast<float>(layer->opacity), 1.0f);

    if (layer->isGroup()) {
        premult = sampleLayerStackAt(layer->children, x, y, layerOpacity);
    } else if (layer->isPixelLayer()) {
        premult = sampleRasterLayerPixel(layer, x, y);
        premult.r *= layerOpacity;
        premult.g *= layerOpacity;
        premult.b *= layerOpacity;
        premult.a *= layerOpacity;
    } else if (layer->isBackground() && layer->backgroundTransparent) {
        return QColor(0, 0, 0, 0);
    } else if (layer->isBackground() && !layer->backgroundTransparent) {
        const QColor bg = layer->backgroundColor;
        const float a = static_cast<float>(bg.alphaF()) * layerOpacity;
        premult.r = static_cast<float>(bg.redF()) * a;
        premult.g = static_cast<float>(bg.greenF()) * a;
        premult.b = static_cast<float>(bg.blueF()) * a;
        premult.a = a;
    } else {
        return QColor(0, 0, 0, 0);
    }

    if (premult.a <= 0.0f)
        return QColor(0, 0, 0, 0);

    const int a = qBound(0, static_cast<int>(std::lround(premult.a * 255.0f)), 255);
    const int r = qBound(0, static_cast<int>(std::lround((premult.r / premult.a) * 255.0f)), 255);
    const int g = qBound(0, static_cast<int>(std::lround((premult.g / premult.a) * 255.0f)), 255);
    const int b = qBound(0, static_cast<int>(std::lround((premult.b / premult.a) * 255.0f)), 255);
    return QColor(r, g, b, static_cast<int>(a));
}

QColor sampleColorFromLayerModel(
    const ruwa::core::layers::LayerModel* layerModel, const aether::Canvas& canvas, int x, int y)
{
    if (!layerModel || x < 0 || y < 0 || x >= static_cast<int>(canvas.width())
        || y >= static_cast<int>(canvas.height())) {
        return QColor(0, 0, 0, 0);
    }

    const PremultipliedColor premult = sampleLayerStackAt(layerModel->rootLayers(), x, y, 1.0f);
    if (premult.a <= 0.0f) {
        return QColor(0, 0, 0, 0);
    }

    const int a = qBound(0, static_cast<int>(std::lround(premult.a * 255.0f)), 255);
    const int r = qBound(0, static_cast<int>(std::lround((premult.r / premult.a) * 255.0f)), 255);
    const int g = qBound(0, static_cast<int>(std::lround((premult.g / premult.a) * 255.0f)), 255);
    const int b = qBound(0, static_cast<int>(std::lround((premult.b / premult.a) * 255.0f)), 255);
    return QColor(r, g, b, static_cast<int>(a));
}

// ==========================================================================
//   T R A N S F O R M   C U R S O R
// ==========================================================================

Qt::CursorShape cursorForTransformHandle(
    const aether::TransformHitResult& hit, bool cornersActAsRotationHandles)
{
    if (cornersActAsRotationHandles) {
        switch (hit.handle) {
        case aether::TransformHandle::TopLeft:
        case aether::TransformHandle::TopRight:
        case aether::TransformHandle::BottomRight:
        case aether::TransformHandle::BottomLeft:
            if (hit.classicCornerRotationAffordance) {
                return Qt::CrossCursor;
            }
            break;
        default:
            break;
        }
    }
    return cursorForTransformHandle(hit.handle, cornersActAsRotationHandles);
}

Qt::CursorShape cursorForTransformHandle(
    aether::TransformHandle handle, bool cornersActAsRotationHandles)
{
    Q_UNUSED(cornersActAsRotationHandles);
    switch (handle) {
    case aether::TransformHandle::TopLeft:
    case aether::TransformHandle::BottomRight:
        return Qt::SizeFDiagCursor;
    case aether::TransformHandle::TopRight:
    case aether::TransformHandle::BottomLeft:
        return Qt::SizeBDiagCursor;
    case aether::TransformHandle::Top:
    case aether::TransformHandle::Bottom:
        return Qt::SizeVerCursor;
    case aether::TransformHandle::Left:
    case aether::TransformHandle::Right:
        return Qt::SizeHorCursor;
    case aether::TransformHandle::Rotate:
        return Qt::CrossCursor;
    case aether::TransformHandle::Move:
        return Qt::SizeAllCursor;
    case aether::TransformHandle::DeformPoint:
        return Qt::SizeAllCursor;
    default:
        return Qt::ArrowCursor;
    }
}

// ==========================================================================
//   A N G L E   U T I L S
// ==========================================================================

float normalizeAngleDelta(float delta)
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.0f;
    while (delta > kPi)
        delta -= kTwoPi;
    while (delta < -kPi)
        delta += kTwoPi;
    return delta;
}

bool isAngleEffectivelyZero(float radians)
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.0f;
    constexpr float kEpsilon = 0.001f;
    const float wrapped = std::fmod(std::abs(radians), kTwoPi);
    return wrapped <= kEpsilon || std::abs(kTwoPi - wrapped) <= kEpsilon;
}

// ==========================================================================
//   I M A G E   I M P O R T
// ==========================================================================

std::shared_ptr<aether::TileGrid> buildTileGridFromImage(QImage image)
{
    if (image.isNull()) {
        return nullptr;
    }

    image = image.convertToFormat(QImage::Format_RGBA8888);
    auto tileGrid = std::make_shared<aether::TileGrid>();
    if (!tileGrid) {
        return nullptr;
    }
    // Source is an 8-bit QImage and the per-pixel copy below writes raw RGBA8
    // premultiplied bytes, so pin the imported grid to RGBA8 (a deliberate fixed
    // format). Without this the grid would default to kDefaultTileFormat and the
    // 8-bit writes would land in a mis-sized 16F/32F buffer. The layer stays
    // self-describing RGBA8 inside a higher-precision document.
    tileGrid->setFormat(aether::TilePixelFormat::RGBA8);

    const int width = image.width();
    const int height = image.height();
    constexpr int tileSize = static_cast<int>(aether::TILE_SIZE);

    for (int tileY = 0; tileY < height; tileY += tileSize) {
        const int tileHeight = std::min(tileSize, height - tileY);
        for (int tileX = 0; tileX < width; tileX += tileSize) {
            const int tileWidth = std::min(tileSize, width - tileX);
            aether::TileData* tile = nullptr;
            uint8_t* tilePixels = nullptr;

            for (int localY = 0; localY < tileHeight; ++localY) {
                const uchar* src = image.constScanLine(tileY + localY) + (tileX * 4);
                for (int localX = 0; localX < tileWidth; ++localX, src += 4) {
                    const uint8_t a = src[3];
                    if (a == 0) {
                        continue;
                    }

                    if (!tile) {
                        const aether::TileKey key { tileX / tileSize, tileY / tileSize };
                        tile = &tileGrid->getOrCreateTile(key);
                        tilePixels = tile->pixels();
                    }

                    const size_t dstIndex = static_cast<size_t>((localY * tileSize + localX) * 4);
                    if (a == 255) {
                        tilePixels[dstIndex + 0] = src[0];
                        tilePixels[dstIndex + 1] = src[1];
                        tilePixels[dstIndex + 2] = src[2];
                        tilePixels[dstIndex + 3] = a;
                        continue;
                    }

                    tilePixels[dstIndex + 0] = static_cast<uint8_t>(
                        (static_cast<int>(src[0]) * static_cast<int>(a) + 127) / 255);
                    tilePixels[dstIndex + 1] = static_cast<uint8_t>(
                        (static_cast<int>(src[1]) * static_cast<int>(a) + 127) / 255);
                    tilePixels[dstIndex + 2] = static_cast<uint8_t>(
                        (static_cast<int>(src[2]) * static_cast<int>(a) + 127) / 255);
                    tilePixels[dstIndex + 3] = a;
                }
            }
        }
    }

    return tileGrid;
}

std::shared_ptr<aether::TileGrid> cloneTileGrid(const aether::TileGrid& source)
{
    auto clone = std::make_shared<aether::TileGrid>();
    if (!clone) {
        return nullptr;
    }

    // Preserve the source grid's pixel format so a 16F/32F content grid is not
    // truncated to a 256 KB RGBA8 slice (setFormat stamps every tile the clone
    // creates, so getOrCreateTile below sizes the destination buffer correctly).
    clone->setFormat(source.format());
    const size_t tileBytes = aether::tileByteSize(source.format());
    for (const auto& [key, tile] : source.tiles()) {
        auto& dstTile = clone->getOrCreateTile(key);
        std::memcpy(dstTile.pixels(), tile.pixels(), tileBytes);
        dstTile.markDirty();
    }

    return clone;
}

ImportedLayerPayload buildLayerPayloadFromImage(
    QImage image, const QString& layerName, ruwa::core::layers::LayerType layerType)
{
    if (image.isNull()) {
        return {};
    }

    const int width = image.width();
    const int height = image.height();
    auto pixelGrid = buildTileGridFromImage(std::move(image));
    if (!pixelGrid) {
        return {};
    }

    ImportedLayerPayload payload;
    payload.id = QUuid::createUuid();
    payload.layerName = layerName;
    payload.layerType = layerType;
    payload.contentBounds = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) };
    payload.pixelGrid = std::move(pixelGrid);
    return payload;
}

std::shared_ptr<ruwa::core::layers::LayerData> materializeImportedLayer(
    ImportedLayerPayload& payload)
{
    if (!payload.isValid()) {
        return nullptr;
    }

    auto layer = ruwa::core::layers::LayerData::create(payload.layerType, payload.layerName);
    auto* tileGrid = layer ? layer->pixelGrid() : nullptr;
    if (!layer || !tileGrid || !payload.pixelGrid) {
        return nullptr;
    }

    layer->id = payload.id;
    if (layer->isRaster()) {
        layer->tileGrid = std::make_unique<aether::TileGrid>(std::move(*payload.pixelGrid));
    } else {
        layer->smartContentGrid = std::make_unique<aether::TileGrid>(std::move(*payload.pixelGrid));
    }
    layer->smartTransform.contentBounds = payload.contentBounds;
    layer->smartTransform.pivot = payload.contentBounds.center();
    layer->smartTransform.reset();
    layer->thumbnailDirty = true;
    return layer;
}

static std::shared_ptr<ruwa::core::layers::LayerData> buildLayerFromImage(
    QImage image, const QString& layerName, ruwa::core::layers::LayerType layerType)
{
    ImportedLayerPayload payload
        = buildLayerPayloadFromImage(std::move(image), layerName, layerType);
    return materializeImportedLayer(payload);
}

std::shared_ptr<ruwa::core::layers::LayerData> buildSmartLayerFromImage(
    QImage image, const QString& layerName)
{
    return buildLayerFromImage(std::move(image), layerName, ruwa::core::layers::LayerType::Smart);
}

std::shared_ptr<ruwa::core::layers::LayerData> buildBoardLayerFromImage(
    QImage image, const QString& layerName)
{
    return buildLayerFromImage(std::move(image), layerName, ruwa::core::layers::LayerType::Board);
}

void placeImportedSmartLayers(QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers,
    const QRect& frame, bool fitToFrame)
{
    const int cw = frame.width();
    const int ch = frame.height();
    if (cw <= 0 || ch <= 0) {
        return;
    }
    const float canvasW = static_cast<float>(cw);
    const float canvasH = static_cast<float>(ch);
    const QPointF frameCenter = frame.center();

    for (const auto& layerPtr : layers) {
        if (!layerPtr || !layerPtr->isIsolatedPixelLayer()) {
            continue;
        }
        auto& t = layerPtr->smartTransform;
        const float w = t.contentBounds.width;
        const float h = t.contentBounds.height;
        if (w <= 0.0f || h <= 0.0f) {
            continue;
        }

        float s = 1.0f;
        if (fitToFrame) {
            s = std::min(canvasW / w, canvasH / h);
            if (s > 1.0f) {
                s = 1.0f;
            }
        }

        t.reset();
        t.scale.x = s;
        t.scale.y = s;
        t.translation.x = static_cast<float>(frameCenter.x()) + 0.5f - t.pivot.x;
        t.translation.y = static_cast<float>(frameCenter.y()) + 0.5f - t.pivot.y;
        layerPtr->thumbnailDirty = true;
    }
}

static ImportedLayerBatch buildImportedLayerBatch(
    const QStringList& filePaths, ruwa::core::layers::LayerType layerType)
{
    ImportedLayerBatch batch;
    batch.layers.reserve(filePaths.size());
    batch.undoLayers.reserve(filePaths.size());

    for (const QString& filePath : filePaths) {
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            continue;
        }

        const QFileInfo fileInfo(filePath);
        const QString layerName = fileInfo.completeBaseName().isEmpty()
            ? fileInfo.fileName()
            : fileInfo.completeBaseName();

        ImportedLayerPayload payload = buildLayerPayloadFromImage(image, layerName, layerType);
        if (!payload.isValid() || !payload.pixelGrid) {
            continue;
        }

        auto undoGrid = cloneTileGrid(*payload.pixelGrid);
        if (!undoGrid) {
            continue;
        }

        ImportedLayerPayload undoPayload = payload;
        undoPayload.pixelGrid = std::move(undoGrid);

        batch.layers.append(std::move(payload));
        batch.undoLayers.append(std::move(undoPayload));
    }

    return batch;
}

static ImportedLayerBatch buildImportedLayerBatchFromImage(
    QImage image, const QString& layerName, ruwa::core::layers::LayerType layerType)
{
    ImportedLayerBatch batch;

    ImportedLayerPayload payload
        = buildLayerPayloadFromImage(std::move(image), layerName, layerType);
    if (!payload.isValid() || !payload.pixelGrid) {
        return batch;
    }

    auto undoGrid = cloneTileGrid(*payload.pixelGrid);
    if (!undoGrid) {
        return batch;
    }

    ImportedLayerPayload undoPayload = payload;
    undoPayload.pixelGrid = std::move(undoGrid);
    batch.layers.append(std::move(payload));
    batch.undoLayers.append(std::move(undoPayload));
    return batch;
}

QList<std::shared_ptr<ruwa::core::layers::LayerData>> buildImportedSmartLayers(
    const QStringList& filePaths)
{
    return materializeImportedLayers(
        buildImportedLayerBatch(filePaths, ruwa::core::layers::LayerType::Smart).layers);
}

QList<std::shared_ptr<ruwa::core::layers::LayerData>> buildImportedBoardLayers(
    const QStringList& filePaths)
{
    return materializeImportedLayers(
        buildImportedLayerBatch(filePaths, ruwa::core::layers::LayerType::Board).layers);
}

ImportedLayerBatch buildImportedSmartLayerBatch(const QStringList& filePaths)
{
    return buildImportedLayerBatch(filePaths, ruwa::core::layers::LayerType::Smart);
}

ImportedLayerBatch buildImportedBoardLayerBatch(const QStringList& filePaths)
{
    return buildImportedLayerBatch(filePaths, ruwa::core::layers::LayerType::Board);
}

ImportedLayerBatch buildImportedRasterLayerBatch(const QStringList& filePaths)
{
    return buildImportedLayerBatch(filePaths, ruwa::core::layers::LayerType::Raster);
}

ImportedLayerBatch buildImportedSmartLayerBatchFromImage(QImage image, const QString& layerName)
{
    return buildImportedLayerBatchFromImage(
        std::move(image), layerName, ruwa::core::layers::LayerType::Smart);
}

ImportedLayerBatch buildImportedBoardLayerBatchFromImage(QImage image, const QString& layerName)
{
    return buildImportedLayerBatchFromImage(
        std::move(image), layerName, ruwa::core::layers::LayerType::Board);
}

ImportedLayerBatch buildImportedRasterLayerBatchFromImage(QImage image, const QString& layerName)
{
    return buildImportedLayerBatchFromImage(
        std::move(image), layerName, ruwa::core::layers::LayerType::Raster);
}

QList<std::shared_ptr<ruwa::core::layers::LayerData>> materializeImportedLayers(
    QList<ImportedLayerPayload> payloads)
{
    QList<std::shared_ptr<ruwa::core::layers::LayerData>> layers;
    layers.reserve(payloads.size());

    for (auto& payload : payloads) {
        auto layer = materializeImportedLayer(payload);
        if (layer) {
            layers.append(std::move(layer));
        }
    }

    return layers;
}

QImage decodeImageData(const QByteArray& data, const QByteArray& formatHint)
{
    return decodeImageDataInternal(data, formatHint);
}

QString suggestedLayerNameForImageUrl(const QUrl& url)
{
    const QString fileName = QFileInfo(url.path()).completeBaseName().trimmed();
    if (!fileName.isEmpty()) {
        return fileName;
    }
    return QObject::tr("Dropped image");
}

QList<QUrl> extractRemoteImageUrlsFromMime(const QMimeData* mimeData)
{
    QList<QUrl> result;
    QSet<QString> seen;
    if (!mimeData) {
        return result;
    }

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl& url : urls) {
            appendUniqueRemoteImageUrl(result, seen, url);
        }
    }

    const QString plainText = mimeData->text().trimmed();
    if (!plainText.isEmpty()) {
        appendUniqueRemoteImageUrl(result, seen, QUrl::fromUserInput(plainText));
    }

    const QList<QUrl> htmlUrls = extractImageUrlsFromHtml(mimeData->html());
    for (const QUrl& url : htmlUrls) {
        appendUniqueRemoteImageUrl(result, seen, url);
    }

    return result;
}

ImportableMimeImage extractImageFromMime(
    const QMimeData* mimeData, const QString& fallbackLayerName)
{
    if (!mimeData) {
        return {};
    }

    const QStringList formats = mimeData->formats();
    for (const QString& mimeFormat : formats) {
        const QString imageFormat = imageFormatFromMimeFormat(mimeFormat);
        if (imageFormat.isEmpty()) {
            continue;
        }

        const QByteArray imageData = mimeData->data(mimeFormat);
        QImage image = decodeImageDataInternal(imageData, imageFormat.toLatin1());
        if (!image.isNull()) {
            return makeImportableMimeImage(std::move(image), fallbackLayerName);
        }
    }

    QImage image = extractImageVariant(mimeData);
    if (!image.isNull()) {
        return makeImportableMimeImage(std::move(image), fallbackLayerName);
    }

    image = decodeDataImageUrl(mimeData->text());
    if (!image.isNull()) {
        return makeImportableMimeImage(std::move(image), fallbackLayerName);
    }

    image = decodeDataImageUrl(firstImageSrcFromHtml(mimeData->html()));
    if (!image.isNull()) {
        return makeImportableMimeImage(std::move(image), fallbackLayerName);
    }

    return {};
}

bool mayContainImportableImageFromMime(const QMimeData* mimeData)
{
    if (!mimeData) {
        return false;
    }
    if (mimeData->hasImage()) {
        return true;
    }

    const QStringList formats = mimeData->formats();
    for (const QString& mimeFormat : formats) {
        if (!imageFormatFromMimeFormat(mimeFormat).isEmpty()) {
            return true;
        }
    }
    if (isDataImageUrl(mimeData->text())
        || isDataImageUrl(firstImageSrcFromHtml(mimeData->html()))) {
        return true;
    }

    return mayContainImportableImagePathsFromMime(mimeData)
        || !extractRemoteImageUrlsFromMime(mimeData).isEmpty();
}

bool mayContainImportableImagePathsFromMime(const QMimeData* mimeData)
{
    if (!mimeData) {
        return false;
    }
    if (!mimeData->hasUrls()) {
        return false;
    }

    const QSet<QString>& suffixes = supportedImageSuffixes();
    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }

        const QFileInfo fileInfo(url.toLocalFile());
        const QString suffix = fileInfo.suffix().trimmed().toLower();
        if (!suffix.isEmpty() && suffixes.contains(suffix)) {
            return true;
        }
    }
    return false;
}

QStringList extractImportableImagePathsFromMime(const QMimeData* mimeData)
{
    QStringList result;
    if (!mimeData || !mimeData->hasUrls()) {
        return result;
    }

    const QList<QUrl> urls = mimeData->urls();
    result.reserve(urls.size());

    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }

        const QString localPath = url.toLocalFile();
        QFileInfo fileInfo(localPath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }

        QImageReader reader(localPath);
        if (!reader.canRead()) {
            continue;
        }

        result.append(fileInfo.absoluteFilePath());
    }
    return result;
}

} // namespace ruwa::ui::workspace::detail
