// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   T R A N S F O R M   C O M M A N D
// ==========================================================================

#include "TransformCommand.h"
#include "shared/undo/SelectionState.h"

#include "features/canvas/scene/Canvas.h"
#include "features/canvas/rendering/TextRetainedPayloadBuilder.h"
#include "features/effects/EffectCoverageResolver.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileData.h"
#include "shared/tiles/TilePixelAccess.h"
#include "features/layers/model/LayerModel.h"
#include "features/layers/model/LayerData.h"
#include <QSet>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <thread>

namespace aether {
namespace {

template <typename MapType>
qint64 estimateUnorderedMapOverhead(const MapType& map, qint64 perNodeExtraBytes = 48)
{
    const qint64 bucketOverhead
        = static_cast<qint64>(map.bucket_count()) * static_cast<qint64>(sizeof(void*));
    const qint64 nodeOverhead = static_cast<qint64>(map.size())
        * static_cast<qint64>(sizeof(typename MapType::value_type) + perNodeExtraBytes);
    return bucketOverhead + nodeOverhead;
}

template <typename SetType>
qint64 estimateUnorderedSetOverhead(const SetType& set, qint64 perNodeExtraBytes = 32)
{
    const qint64 bucketOverhead
        = static_cast<qint64>(set.bucket_count()) * static_cast<qint64>(sizeof(void*));
    const qint64 nodeOverhead = static_cast<qint64>(set.size())
        * static_cast<qint64>(sizeof(typename SetType::value_type) + perNodeExtraBytes);
    return bucketOverhead + nodeOverhead;
}

using RawTileMap = std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>;
using CompressedTileMap = std::unordered_map<TileKey, QByteArray, TileKeyHash>;
constexpr size_t kParallelTileCodecThreshold = 4;

std::unordered_set<TileKey, TileKeyHash> retainedTextKeysForLayer(
    Canvas* canvas, ruwa::core::layers::LayerData* layer)
{
    if (layer && layer->runtimeRetainedPayload && !layer->runtimeRetainedPayload->empty()) {
        return retainedCoverageTileKeys(layer->runtimeRetainedPayload->worldBounds);
    }
    if (canvas && layer) {
        return canvas->tilePositionIndex().tileKeysForLayer(layer->id);
    }
    return {};
}

void clearTextTransformRuntimeCaches(ruwa::core::layers::LayerData* layer)
{
    if (!layer) {
        return;
    }

    layer->runtimeRetainedPayload.reset();
    layer->runtimeRetainedPayloadKey.clear();
}

void applyTextTransformState(
    Canvas* canvas, ruwa::core::layers::LayerData* layer, const TransformState& state)
{
    if (!layer || !layer->isText() || !layer->textData) {
        return;
    }

    std::unordered_set<TileKey, TileKeyHash> affected = retainedTextKeysForLayer(canvas, layer);
    layer->textData->transform = state;
    clearTextTransformRuntimeCaches(layer);

    std::unordered_set<TileKey, TileKeyHash> newKeys;
    if (ensureTextRetainedPayload(layer) && layer->runtimeRetainedPayload) {
        newKeys = retainedCoverageTileKeys(layer->runtimeRetainedPayload->worldBounds);
    }

    if (canvas) {
        canvas->tilePositionIndex().removeLayer(layer->id);
        for (const TileKey& key : newKeys) {
            canvas->tilePositionIndex().addEntry(key, layer->id);
            affected.insert(key);
        }
        if (!affected.empty()) {
            canvas->dirtyManager().onTilesDirtied(layer->id, affected);
        } else {
            canvas->dirtyManager().onStructureChanged();
        }
    }
}

inline int floorDiv(int value, int divisor)
{
    int q = value / divisor;
    const int r = value % divisor;
    if (r < 0) {
        --q;
    }
    return q;
}

bool tileHasVisiblePixels(const std::vector<uint8_t>& pixels, TilePixelFormat fmt)
{
    // RawTileMap buffers carry no format tag; the format is threaded in from the
    // owning command (m_contentFormat) so this stays correct under per-document
    // formats instead of assuming 8-bit stride/alpha.
    if (pixels.size() != tileByteSize(fmt)) {
        return false;
    }
    for (uint32_t y = 0; y < TILE_SIZE; ++y) {
        for (uint32_t x = 0; x < TILE_SIZE; ++x) {
            if (!tilePixelAlphaIsZero(pixels.data(), fmt, x, y)) {
                return true;
            }
        }
    }
    return false;
}

void pruneTransparentTiles(RawTileMap& tiles, TilePixelFormat fmt)
{
    for (auto it = tiles.begin(); it != tiles.end();) {
        if (!tileHasVisiblePixels(it->second, fmt)) {
            it = tiles.erase(it);
        } else {
            ++it;
        }
    }
}

RawTileMap remapRawTilesTileAligned(const RawTileMap& src, int offsetX, int offsetY, int newWidth,
    int newHeight, TilePixelFormat fmt)
{
    RawTileMap dst;
    const int bpp = static_cast<int>(tileBytesPerPixel(fmt));
    const int tileSize = static_cast<int>(TILE_SIZE);
    const int tileOffX = offsetX / tileSize;
    const int tileOffY = offsetY / tileSize;
    const int maxDstTX = (newWidth - 1) / tileSize;
    const int maxDstTY = (newHeight - 1) / tileSize;

    dst.reserve(src.size());
    for (const auto& [srcKey, pixels] : src) {
        if (pixels.size() != tileByteSize(fmt)) {
            continue;
        }
        const TileKey dstKey { srcKey.x - tileOffX, srcKey.y - tileOffY };
        if (dstKey.x < 0 || dstKey.y < 0 || dstKey.x > maxDstTX || dstKey.y > maxDstTY) {
            continue;
        }

        auto& dstPixels = dst[dstKey];
        dstPixels = pixels;

        const int localMaxX = std::min(tileSize, newWidth - dstKey.x * tileSize);
        const int localMaxY = std::min(tileSize, newHeight - dstKey.y * tileSize);
        if (localMaxY < tileSize && localMaxY >= 0) {
            std::memset(dstPixels.data() + localMaxY * tileSize * bpp, 0,
                static_cast<size_t>(tileSize - localMaxY) * tileSize * bpp);
        }
        if (localMaxX < tileSize && localMaxX >= 0) {
            for (int y = 0; y < localMaxY; ++y) {
                std::memset(dstPixels.data() + (y * tileSize + localMaxX) * bpp, 0,
                    static_cast<size_t>(tileSize - localMaxX) * bpp);
            }
        }
    }

    pruneTransparentTiles(dst, fmt);
    return dst;
}

RawTileMap remapRawTiles(const RawTileMap& src, int offsetX, int offsetY, int newWidth,
    int newHeight, TilePixelFormat fmt)
{
    RawTileMap dst;
    if (newWidth <= 0 || newHeight <= 0) {
        return dst;
    }

    const int bpp = static_cast<int>(tileBytesPerPixel(fmt));
    const int tileSize = static_cast<int>(TILE_SIZE);
    if ((offsetX % tileSize == 0) && (offsetY % tileSize == 0)) {
        return remapRawTilesTileAligned(src, offsetX, offsetY, newWidth, newHeight, fmt);
    }

    dst.reserve(src.size() * 2);
    for (const auto& [srcKey, pixels] : src) {
        if (pixels.size() != tileByteSize(fmt)) {
            continue;
        }

        const int dstLeft = srcKey.x * tileSize - offsetX;
        const int dstTop = srcKey.y * tileSize - offsetY;
        const int dstRight = dstLeft + tileSize;
        const int dstBottom = dstTop + tileSize;

        const int clippedLeft = std::max(0, dstLeft);
        const int clippedTop = std::max(0, dstTop);
        const int clippedRight = std::min(newWidth, dstRight);
        const int clippedBottom = std::min(newHeight, dstBottom);
        if (clippedLeft >= clippedRight || clippedTop >= clippedBottom) {
            continue;
        }

        const int dstTileMinX = floorDiv(clippedLeft, tileSize);
        const int dstTileMaxX = floorDiv(clippedRight - 1, tileSize);
        const int dstTileMinY = floorDiv(clippedTop, tileSize);
        const int dstTileMaxY = floorDiv(clippedBottom - 1, tileSize);

        for (int dstTy = dstTileMinY; dstTy <= dstTileMaxY; ++dstTy) {
            for (int dstTx = dstTileMinX; dstTx <= dstTileMaxX; ++dstTx) {
                const int tileLeft = dstTx * tileSize;
                const int tileTop = dstTy * tileSize;
                const int copyLeft = std::max(clippedLeft, tileLeft);
                const int copyTop = std::max(clippedTop, tileTop);
                const int copyRight = std::min(clippedRight, tileLeft + tileSize);
                const int copyBottom = std::min(clippedBottom, tileTop + tileSize);
                const int copyWidth = copyRight - copyLeft;
                const int copyHeight = copyBottom - copyTop;
                if (copyWidth <= 0 || copyHeight <= 0) {
                    continue;
                }

                auto& dstPixels = dst[TileKey { dstTx, dstTy }];
                if (dstPixels.empty()) {
                    dstPixels.resize(tileByteSize(fmt), 0);
                }

                const int srcLocalX = copyLeft - dstLeft;
                const int srcLocalY = copyTop - dstTop;
                const int dstLocalX = copyLeft - tileLeft;
                const int dstLocalY = copyTop - tileTop;
                const size_t rowBytes = static_cast<size_t>(copyWidth) * bpp;
                const int stride = tileSize * bpp;

                for (int y = 0; y < copyHeight; ++y) {
                    const uint8_t* srcRow = pixels.data()
                        + static_cast<size_t>(srcLocalY + y) * stride
                        + static_cast<size_t>(srcLocalX) * bpp;
                    uint8_t* dstRow = dstPixels.data() + static_cast<size_t>(dstLocalY + y) * stride
                        + static_cast<size_t>(dstLocalX) * bpp;
                    std::memcpy(dstRow, srcRow, rowBytes);
                }
            }
        }
    }

    pruneTransparentTiles(dst, fmt);
    return dst;
}

CompressedTileMap remapCompressedTilesTileAligned(const CompressedTileMap& src, int offsetX,
    int offsetY, int newWidth, int newHeight, TilePixelFormat fmt)
{
    CompressedTileMap dst;
    if (newWidth <= 0 || newHeight <= 0) {
        return dst;
    }

    const int tileSize = static_cast<int>(TILE_SIZE);
    const int tileOffX = offsetX / tileSize;
    const int tileOffY = offsetY / tileSize;
    const int maxDstTX = (newWidth - 1) / tileSize;
    const int maxDstTY = (newHeight - 1) / tileSize;

    RawTileMap edgeTiles;
    dst.reserve(src.size());
    for (const auto& [srcKey, compData] : src) {
        const TileKey dstKey { srcKey.x - tileOffX, srcKey.y - tileOffY };
        if (dstKey.x < 0 || dstKey.y < 0 || dstKey.x > maxDstTX || dstKey.y > maxDstTY) {
            continue;
        }

        const bool fullyInsideCanvas
            = (dstKey.x + 1) * tileSize <= newWidth && (dstKey.y + 1) * tileSize <= newHeight;
        if (fullyInsideCanvas) {
            dst.emplace(dstKey, compData);
            continue;
        }

        const QByteArray raw = qUncompress(compData);
        if (static_cast<size_t>(raw.size()) != tileByteSize(fmt)) {
            continue;
        }
        std::vector<uint8_t> pixels(tileByteSize(fmt));
        std::memcpy(pixels.data(), raw.constData(), tileByteSize(fmt));
        edgeTiles.emplace(srcKey, std::move(pixels));
    }

    RawTileMap remappedEdges
        = remapRawTilesTileAligned(edgeTiles, offsetX, offsetY, newWidth, newHeight, fmt);
    for (auto& [key, pixels] : remappedEdges) {
        QByteArray rawBytes(
            reinterpret_cast<const char*>(pixels.data()), static_cast<qsizetype>(pixels.size()));
        dst.insert_or_assign(key, qCompress(rawBytes, 1));
    }
    return dst;
}

CompressedTileMap remapCompressedTiles(const CompressedTileMap& src, int offsetX, int offsetY,
    int newWidth, int newHeight, TilePixelFormat fmt)
{
    CompressedTileMap dst;
    if (newWidth <= 0 || newHeight <= 0) {
        return dst;
    }

    const int tileSize = static_cast<int>(TILE_SIZE);
    if ((offsetX % tileSize == 0) && (offsetY % tileSize == 0)) {
        return remapCompressedTilesTileAligned(src, offsetX, offsetY, newWidth, newHeight, fmt);
    }

    RawTileMap unpacked;
    unpacked.reserve(src.size());
    for (const auto& [key, compData] : src) {
        const QByteArray raw = qUncompress(compData);
        if (static_cast<size_t>(raw.size()) != tileByteSize(fmt)) {
            continue;
        }
        std::vector<uint8_t> pixels(tileByteSize(fmt));
        std::memcpy(pixels.data(), raw.constData(), tileByteSize(fmt));
        unpacked.emplace(key, std::move(pixels));
    }

    RawTileMap remapped = remapRawTiles(unpacked, offsetX, offsetY, newWidth, newHeight, fmt);
    dst.reserve(remapped.size());
    for (auto& [key, pixels] : remapped) {
        QByteArray rawBytes(
            reinterpret_cast<const char*>(pixels.data()), static_cast<qsizetype>(pixels.size()));
        dst.emplace(key, qCompress(rawBytes, 1));
    }
    return dst;
}

RawTileMap decompressTiles(const CompressedTileMap& src, TilePixelFormat fmt)
{
    if (src.size() < kParallelTileCodecThreshold) {
        RawTileMap dst;
        dst.reserve(src.size());
        for (const auto& [key, compData] : src) {
            const QByteArray raw = qUncompress(compData);
            if (static_cast<size_t>(raw.size()) != tileByteSize(fmt)) {
                continue;
            }
            std::vector<uint8_t> pixels(tileByteSize(fmt));
            std::memcpy(pixels.data(), raw.constData(), tileByteSize(fmt));
            dst.emplace(key, std::move(pixels));
        }
        return dst;
    }

    struct Entry {
        TileKey key;
        const QByteArray* compData;
    };
    std::vector<Entry> entries;
    entries.reserve(src.size());
    for (const auto& [key, compData] : src) {
        entries.push_back({ key, &compData });
    }

    struct Result {
        TileKey key;
        std::vector<uint8_t> pixels;
        bool valid = false;
    };
    std::vector<Result> results(entries.size());

    const size_t numThreads = std::min<size_t>(
        entries.size(), std::max<size_t>(1, std::thread::hardware_concurrency()));
    const size_t chunkSize = (entries.size() + numThreads - 1) / numThreads;

    std::vector<std::future<void>> futures;
    futures.reserve(numThreads);

    for (size_t t = 0; t < numThreads; ++t) {
        const size_t begin = t * chunkSize;
        const size_t end = std::min(begin + chunkSize, entries.size());
        if (begin >= end) {
            break;
        }

        futures.push_back(std::async(std::launch::async, [&entries, &results, begin, end, fmt]() {
            for (size_t i = begin; i < end; ++i) {
                const QByteArray raw = qUncompress(*entries[i].compData);
                if (static_cast<size_t>(raw.size()) != tileByteSize(fmt)) {
                    continue;
                }
                results[i].key = entries[i].key;
                results[i].pixels.resize(tileByteSize(fmt));
                std::memcpy(results[i].pixels.data(), raw.constData(), tileByteSize(fmt));
                results[i].valid = true;
            }
        }));
    }

    for (auto& future : futures) {
        future.get();
    }

    RawTileMap dst;
    dst.reserve(src.size());
    for (auto& result : results) {
        if (result.valid) {
            dst.emplace(result.key, std::move(result.pixels));
        }
    }
    return dst;
}

std::unordered_set<TileKey, TileKeyHash> keySetFromCompressedMap(const CompressedTileMap& map)
{
    std::unordered_set<TileKey, TileKeyHash> keys;
    keys.reserve(map.size());
    for (const auto& [key, _] : map)
        keys.insert(key);
    return keys;
}

void addKeysToPositions(const std::unordered_set<TileKey, TileKeyHash>& keys,
    std::unordered_set<TileKey, TileKeyHash>& positions)
{
    for (const TileKey& key : keys) {
        positions.insert(key);
    }
}

void addMapKeysToPositions(
    const RawTileMap& map, std::unordered_set<TileKey, TileKeyHash>& positions)
{
    for (const auto& [key, _] : map) {
        positions.insert(key);
    }
}

void addRectTilePositions(const Rect& rect, std::unordered_set<TileKey, TileKeyHash>& positions)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }

    const float tileSize = static_cast<float>(TILE_SIZE);
    const int minX = static_cast<int>(std::floor(rect.left() / tileSize));
    const int minY = static_cast<int>(std::floor(rect.top() / tileSize));
    const int maxX = static_cast<int>(std::ceil(rect.right() / tileSize)) - 1;
    const int maxY = static_cast<int>(std::ceil(rect.bottom() / tileSize)) - 1;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            positions.insert(TileKey { x, y });
        }
    }
}

QList<QPoint> tilePositionsFromKeys(const std::unordered_set<TileKey, TileKeyHash>& keys)
{
    QList<QPoint> points;
    points.reserve(static_cast<qsizetype>(keys.size()));
    for (const TileKey& key : keys) {
        points.append(QPoint(key.x, key.y));
    }
    return points;
}

QList<QPoint> buildAffectedTilePositions(const RawTileMap& beforeTiles,
    const RawTileMap& afterTiles, const std::unordered_set<TileKey, TileKeyHash>& createdTiles,
    const std::unordered_set<TileKey, TileKeyHash>& removedTiles)
{
    std::unordered_set<TileKey, TileKeyHash> positions;
    positions.reserve(
        beforeTiles.size() + afterTiles.size() + createdTiles.size() + removedTiles.size());
    addMapKeysToPositions(beforeTiles, positions);
    addMapKeysToPositions(afterTiles, positions);
    addKeysToPositions(createdTiles, positions);
    addKeysToPositions(removedTiles, positions);
    return tilePositionsFromKeys(positions);
}

QList<QPoint> buildAffectedTilePositions(const std::unordered_set<TileKey, TileKeyHash>& beforeKeys,
    const std::unordered_set<TileKey, TileKeyHash>& afterKeys,
    const std::unordered_set<TileKey, TileKeyHash>& createdTiles,
    const std::unordered_set<TileKey, TileKeyHash>& removedTiles)
{
    std::unordered_set<TileKey, TileKeyHash> positions;
    positions.reserve(
        beforeKeys.size() + afterKeys.size() + createdTiles.size() + removedTiles.size());
    addKeysToPositions(beforeKeys, positions);
    addKeysToPositions(afterKeys, positions);
    addKeysToPositions(createdTiles, positions);
    addKeysToPositions(removedTiles, positions);
    return tilePositionsFromKeys(positions);
}

QList<QPoint> buildSmartTransformAffectedTilePositions(
    const TransformState& before, const TransformState& after)
{
    std::unordered_set<TileKey, TileKeyHash> positions;
    addRectTilePositions(before.transformedAABB(), positions);
    addRectTilePositions(after.transformedAABB(), positions);
    return tilePositionsFromKeys(positions);
}

QList<QPoint> expandAffectedTilePositionsByEffects(
    const QList<QPoint>& points, const QList<ruwa::core::effects::LayerEffectState>& effects)
{
    if (points.isEmpty() || effects.isEmpty()) {
        return points;
    }

    std::unordered_set<TileKey, TileKeyHash> keys;
    keys.reserve(static_cast<size_t>(points.size()));
    for (const QPoint& point : points) {
        keys.insert(TileKey { point.x(), point.y() });
    }

    keys = ruwa::core::effects::EffectCoverageResolver::expandedDocumentCoverage(keys, effects);
    return tilePositionsFromKeys(keys);
}

std::unordered_set<TileKey, TileKeyHash> setDifference(
    const std::unordered_set<TileKey, TileKeyHash>& a,
    const std::unordered_set<TileKey, TileKeyHash>& b)
{
    std::unordered_set<TileKey, TileKeyHash> out;
    for (const auto& key : a) {
        if (b.count(key) == 0)
            out.insert(key);
    }
    return out;
}

} // namespace

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

TransformCommand::TransformCommand(Canvas* canvas, ruwa::core::layers::LayerModel* layerModel,
    TransformSnapshot&& snapshot, std::optional<SelectionRestoreContext> selectionRestore)
    : m_canvas(canvas)
    , m_layerModel(layerModel)
    , m_layerId(snapshot.layerId)
    , m_createdTiles(std::move(snapshot.createdTiles))
    , m_removedTiles(std::move(snapshot.removedTiles))
    , m_rawBeforeTiles(std::move(snapshot.beforeTiles))
    , m_rawAfterTiles(std::move(snapshot.afterTiles))
    , m_maskTarget(snapshot.maskTarget)
    , m_isSmartTransform(snapshot.isSmartTransform)
    , m_beforeSmartTransform(std::move(snapshot.beforeSmartTransform))
    , m_afterSmartTransform(std::move(snapshot.afterSmartTransform))
    , m_selectionRestore(std::move(selectionRestore))
{
    // Capture the snapshot's pixel format from the target grid (mask targets are
    // RGBA8; content follows the document format). Snapshot byte buffers carry no
    // tag, so this drives their size/interpretation on undo/redo/resize.
    if (const TileGrid* grid = resolveGrid()) {
        m_contentFormat = grid->format();
    }

    m_affectedTilePositions = m_isSmartTransform
        ? buildSmartTransformAffectedTilePositions(m_beforeSmartTransform, m_afterSmartTransform)
        : buildAffectedTilePositions(
              m_rawBeforeTiles, m_rawAfterTiles, m_createdTiles, m_removedTiles);
    if (const auto* layer = resolveLayer()) {
        m_affectedTilePositions
            = expandAffectedTilePositionsByEffects(m_affectedTilePositions, layer->effects);
    }

    m_rawSize = 0;
    for (const auto& [key, data] : m_rawBeforeTiles)
        m_rawSize += static_cast<qint64>(data.capacity());
    for (const auto& [key, data] : m_rawAfterTiles)
        m_rawSize += static_cast<qint64>(data.capacity());

    m_rawSize += estimateUnorderedMapOverhead(m_rawBeforeTiles);
    m_rawSize += estimateUnorderedMapOverhead(m_rawAfterTiles);
    m_rawSize += estimateUnorderedSetOverhead(m_createdTiles);
    m_rawSize += estimateUnorderedSetOverhead(m_removedTiles);

    // Launch background compression
    m_compressFuture = std::async(std::launch::async, [this]() { compressAsync(); });
}

TransformCommand::~TransformCommand()
{
    waitForCompression();
}

// ==========================================================================
//   C O M P R E S S I O N
// ==========================================================================

void TransformCommand::compressAsync()
{
    auto compBefore = compressTiles(m_rawBeforeTiles);
    auto compAfter = compressTiles(m_rawAfterTiles);

    qint64 compSize = 0;
    for (const auto& [key, data] : compBefore)
        compSize += static_cast<qint64>(data.capacity());
    for (const auto& [key, data] : compAfter)
        compSize += static_cast<qint64>(data.capacity());

    compSize += estimateUnorderedMapOverhead(compBefore);
    compSize += estimateUnorderedMapOverhead(compAfter);
    compSize += estimateUnorderedSetOverhead(m_createdTiles);
    compSize += estimateUnorderedSetOverhead(m_removedTiles);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_compBeforeTiles = std::move(compBefore);
        m_compAfterTiles = std::move(compAfter);
        m_compressedSize = compSize;
        RawTileMap {}.swap(m_preparedTiles);
        m_preparedDirection = PreparedDirection::None;
        // Release raw memory (including unordered_map buckets).
        RawTileMap {}.swap(m_rawBeforeTiles);
        RawTileMap {}.swap(m_rawAfterTiles);
        m_compressionDone.store(true, std::memory_order_release);
    }
}

void TransformCommand::waitForCompression() const
{
    if (m_compressFuture.valid()) {
        const_cast<std::future<void>&>(m_compressFuture).wait();
    }
}

TransformCommand::CompressedTileMap TransformCommand::compressTiles(const RawTileMap& raw)
{
    if (raw.size() < kParallelTileCodecThreshold) {
        CompressedTileMap result;
        result.reserve(raw.size());
        for (const auto& [key, pixels] : raw) {
            QByteArray rawBytes(reinterpret_cast<const char*>(pixels.data()),
                static_cast<qsizetype>(pixels.size()));
            result[key] = qCompress(rawBytes, 1);
        }
        return result;
    }

    struct Entry {
        TileKey key;
        const std::vector<uint8_t>* pixels;
    };
    std::vector<Entry> entries;
    entries.reserve(raw.size());
    for (const auto& [key, pixels] : raw) {
        entries.push_back({ key, &pixels });
    }

    std::vector<QByteArray> compressed(entries.size());

    const size_t numThreads = std::min<size_t>(
        entries.size(), std::max<size_t>(1, std::thread::hardware_concurrency()));
    const size_t chunkSize = (entries.size() + numThreads - 1) / numThreads;

    std::vector<std::future<void>> futures;
    futures.reserve(numThreads);

    for (size_t t = 0; t < numThreads; ++t) {
        const size_t begin = t * chunkSize;
        const size_t end = std::min(begin + chunkSize, entries.size());
        if (begin >= end) {
            break;
        }

        futures.push_back(std::async(std::launch::async, [&entries, &compressed, begin, end]() {
            for (size_t i = begin; i < end; ++i) {
                QByteArray rawBytes(reinterpret_cast<const char*>(entries[i].pixels->data()),
                    static_cast<qsizetype>(entries[i].pixels->size()));
                compressed[i] = qCompress(rawBytes, 1);
            }
        }));
    }

    for (auto& future : futures) {
        future.get();
    }

    CompressedTileMap result;
    result.reserve(raw.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        result.emplace(entries[i].key, std::move(compressed[i]));
    }
    return result;
}

// ==========================================================================
//   U N D O  /  R E D O
// ==========================================================================

void TransformCommand::undo()
{
    if (m_isSmartTransform) {
        if (auto* layer = resolveLayer()) {
            if (layer->isIsolatedPixelLayer()) {
                layer->smartTransform = m_beforeSmartTransform;
            } else if (layer->isText() && layer->textData) {
                applyTextTransformState(m_canvas, layer, m_beforeSmartTransform);
            } else {
                return;
            }
            if (m_canvas) {
                // Non-destructive transform changes can move projected tiles
                // outside current index keys. Force full cache refresh to prevent
                // stale silhouettes in composition cache.
                m_canvas->compositionCache().markAllDirty();
            }
            if (m_layerModel) {
                m_layerModel->notifyLayerDataChanged(layer->id);
            }
        }
        if (m_selectionRestore) {
            applySelectionRestore(*m_selectionRestore, m_selectionRestore->before);
        }
        return;
    }
    flushPendingRemaps();
    applyState(&m_rawBeforeTiles, &m_compBeforeTiles, m_createdTiles, PreparedDirection::Undo);
    if (m_selectionRestore) {
        applySelectionRestore(*m_selectionRestore, m_selectionRestore->before);
    }
}

void TransformCommand::redo()
{
    if (m_isSmartTransform) {
        if (auto* layer = resolveLayer()) {
            if (layer->isIsolatedPixelLayer()) {
                layer->smartTransform = m_afterSmartTransform;
            } else if (layer->isText() && layer->textData) {
                applyTextTransformState(m_canvas, layer, m_afterSmartTransform);
            } else {
                return;
            }
            if (m_canvas) {
                // See undo() note above.
                m_canvas->compositionCache().markAllDirty();
            }
            if (m_layerModel) {
                m_layerModel->notifyLayerDataChanged(layer->id);
            }
        }
        if (m_selectionRestore) {
            applySelectionRestore(*m_selectionRestore, m_selectionRestore->after);
        }
        return;
    }
    flushPendingRemaps();
    applyState(&m_rawAfterTiles, &m_compAfterTiles, m_removedTiles, PreparedDirection::Redo);
    if (m_selectionRestore) {
        applySelectionRestore(*m_selectionRestore, m_selectionRestore->after);
    }
}

// ==========================================================================
//   A P P L Y   S T A T E
// ==========================================================================

void TransformCommand::applyState(const RawTileMap* rawData,
    const CompressedTileMap* compressedData,
    const std::unordered_set<TileKey, TileKeyHash>& tilesToRemove,
    PreparedDirection preparedDirection)
{
    TileGrid* grid = resolveGrid();
    if (!grid)
        return;

    RawTileMap decompressedTarget;
    const RawTileMap* targetMap = rawData;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_compressionDone.load(std::memory_order_acquire)) {
            if (m_preparedDirection == preparedDirection) {
                targetMap = &m_preparedTiles;
            } else {
                decompressedTarget = decompressTiles(*compressedData, m_contentFormat);
                targetMap = &decompressedTarget;
            }
        } else {
            // Compression still running: compressAsync() frees the raw tile
            // maps under m_mutex when it finishes, so the raw-snapshot apply
            // must complete without releasing the lock (same undo-after-
            // stroke use-after-free as DrawCommand::applyState).
            // applyResolvedState never takes m_mutex, so no deadlock.
            applyResolvedState(*rawData, tilesToRemove);
            return;
        }
    }

    applyResolvedState(*targetMap, tilesToRemove);
}

void TransformCommand::applyResolvedState(
    const RawTileMap& targetMap, const std::unordered_set<TileKey, TileKeyHash>& tilesToRemove)
{
    TileGrid* grid = resolveGrid();
    if (!grid) {
        return;
    }

    std::unordered_set<TileKey, TileKeyHash> affected;
    // Format-sized opaque transport (matches DrawCommand::applyResolvedState):
    // RGBA8 == TILE_BYTE_SIZE; all-zero compare is valid for any format.
    const size_t bytesPerTile = tileByteSize(grid->format());
    static const std::vector<uint8_t> kZeroTile(tileMaxByteSize(), 0);

    affected.reserve(targetMap.size() + tilesToRemove.size());

    for (const auto& [key, targetPixels] : targetMap) {
        if (targetPixels.size() != bytesPerTile) {
            continue;
        }

        const uint8_t* targetBytes = targetPixels.data();
        const bool targetIsZero = std::memcmp(targetBytes, kZeroTile.data(), bytesPerTile) == 0;

        TileData* tile = grid->getTile(key);
        if (tile) {
            if (std::memcmp(tile->pixels(), targetBytes, bytesPerTile) == 0) {
                continue;
            }
        } else if (targetIsZero) {
            continue;
        }

        if (!tile) {
            tile = &grid->getOrCreateTile(key);
        }

        std::memcpy(tile->pixels(), targetBytes, bytesPerTile);

        tile->markDirty();
        grid->markDirty(key);

        if (targetIsZero) {
            grid->removeTile(key);
            // Mask tiles are not layer content — they must not register in the
            // content position index (used for viewport culling / hit-testing).
            if (!m_maskTarget) {
                m_canvas->tilePositionIndex().removeEntry(key, m_layerId);
            }
        } else if (!m_maskTarget) {
            m_canvas->tilePositionIndex().addEntry(key, m_layerId);
        }
        affected.insert(key);
    }

    for (const TileKey& key : tilesToRemove) {
        if (targetMap.count(key)) {
            continue;
        }

        TileData* tile = grid->getTile(key);
        if (!tile) {
            continue;
        }

        grid->removeTile(key);
        if (!m_maskTarget) {
            m_canvas->tilePositionIndex().removeEntry(key, m_layerId);
        }
        grid->markDirty(key);
        affected.insert(key);
    }

    if (!affected.empty()) {
        std::unordered_set<TileKey, TileKeyHash> compositionAffected = affected;
        if (const auto* layer = resolveLayer()) {
            compositionAffected
                = ruwa::core::effects::EffectCoverageResolver::expandedDocumentCoverage(
                    compositionAffected, layer->effects);
        }
        m_canvas->dirtyManager().onTilesDirtied(m_layerId, compositionAffected);
        if (m_maskTarget && m_layerModel) {
            // The mask gates compositing and drives the mask thumbnail; refresh
            // the cached layer stack + panel so undo/redo of a mask transform is
            // visible, mirroring the forward apply's notifyLayerDataChanged.
            m_layerModel->notifyLayerDataChanged(m_layerId);
        }
    }
}

// ==========================================================================
//   H E L P E R S
// ==========================================================================

TileGrid* TransformCommand::resolveGrid() const
{
    if (!m_layerModel)
        return nullptr;

    TileGrid* result = nullptr;
    m_layerModel->forEach([&](ruwa::core::layers::LayerData* layer) {
        if (layer->id == m_layerId && layer->isPixelLayer()) {
            result = m_maskTarget ? layer->maskTileGrid() : layer->pixelGrid();
        }
    });
    return result;
}

ruwa::core::layers::LayerData* TransformCommand::resolveLayer() const
{
    if (!m_layerModel)
        return nullptr;
    return m_layerModel->layerById(m_layerId);
}

QString TransformCommand::text() const
{
    return QStringLiteral("Transform");
}

qint64 TransformCommand::memorySize() const
{
    if (m_compressionDone.load(std::memory_order_acquire)) {
        return m_compressedSize;
    }
    return m_rawSize;
}

bool TransformCommand::requiresAsyncPreparationForUndo() const
{
    if (m_isSmartTransform) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_pendingRemaps.empty()) {
            return true;
        }
    }
    if (!m_compressionDone.load(std::memory_order_acquire)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    return m_preparedDirection != PreparedDirection::Undo;
}

bool TransformCommand::requiresAsyncPreparationForRedo() const
{
    if (m_isSmartTransform) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_pendingRemaps.empty()) {
            return true;
        }
    }
    if (!m_compressionDone.load(std::memory_order_acquire)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    return m_preparedDirection != PreparedDirection::Redo;
}

void TransformCommand::prepareUndo()
{
    prepareCompressedState(PreparedDirection::Undo);
}

void TransformCommand::prepareRedo()
{
    prepareCompressedState(PreparedDirection::Redo);
}

QList<QPoint> TransformCommand::affectedTilePositions() const
{
    return m_affectedTilePositions;
}

void TransformCommand::prepareCompressedState(PreparedDirection direction)
{
    if (m_isSmartTransform) {
        return;
    }
    flushPendingRemaps();
    if (!m_compressionDone.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_preparedDirection == direction) {
        return;
    }

    switch (direction) {
    case PreparedDirection::Undo:
        m_preparedTiles = decompressTiles(m_compBeforeTiles, m_contentFormat);
        break;
    case PreparedDirection::Redo:
        m_preparedTiles = decompressTiles(m_compAfterTiles, m_contentFormat);
        break;
    case PreparedDirection::None:
        RawTileMap {}.swap(m_preparedTiles);
        break;
    }

    m_preparedDirection = direction;
}

bool TransformCommand::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    // O(1): the heavy tile remap is deferred until this command is actually
    // needed (undo/redo/prefetch), so a canvas resize no longer scales with
    // undo-stack depth. Smart-transform state is cheap and shifted eagerly.
    std::lock_guard<std::mutex> lock(m_mutex);
    RawTileMap {}.swap(m_preparedTiles);
    m_preparedDirection = PreparedDirection::None;
    m_pendingRemaps.push_back({ offsetX, offsetY, newWidth, newHeight });

    if (m_isSmartTransform) {
        m_beforeSmartTransform.shiftForCanvasResize(offsetX, offsetY);
        m_afterSmartTransform.shiftForCanvasResize(offsetX, offsetY);
        m_affectedTilePositions = buildSmartTransformAffectedTilePositions(
            m_beforeSmartTransform, m_afterSmartTransform);
        if (const auto* layer = resolveLayer()) {
            m_affectedTilePositions
                = expandAffectedTilePositionsByEffects(m_affectedTilePositions, layer->effects);
        }
    }

    return true;
}

void TransformCommand::flushPendingRemaps()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pendingRemaps.empty()) {
            return;
        }
    }

    // Remap operates on the compressed snapshots; the raw maps may still be
    // read by the background compression thread, so wait it out first.
    waitForCompression();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pendingRemaps.empty()) {
        return;
    }

    for (const auto& remap : m_pendingRemaps) {
        m_compBeforeTiles = remapCompressedTiles(m_compBeforeTiles, remap.offsetX, remap.offsetY,
            remap.newWidth, remap.newHeight, m_contentFormat);
        m_compAfterTiles = remapCompressedTiles(m_compAfterTiles, remap.offsetX, remap.offsetY,
            remap.newWidth, remap.newHeight, m_contentFormat);
    }
    m_pendingRemaps.clear();

    const auto beforeKeys = keySetFromCompressedMap(m_compBeforeTiles);
    const auto afterKeys = keySetFromCompressedMap(m_compAfterTiles);
    m_createdTiles = setDifference(afterKeys, beforeKeys);
    m_removedTiles = setDifference(beforeKeys, afterKeys);
    if (!m_isSmartTransform) {
        m_affectedTilePositions
            = buildAffectedTilePositions(beforeKeys, afterKeys, m_createdTiles, m_removedTiles);
        if (const auto* layer = resolveLayer()) {
            m_affectedTilePositions
                = expandAffectedTilePositionsByEffects(m_affectedTilePositions, layer->effects);
        }
    }

    qint64 compSize = 0;
    for (const auto& [_, data] : m_compBeforeTiles)
        compSize += static_cast<qint64>(data.capacity());
    for (const auto& [_, data] : m_compAfterTiles)
        compSize += static_cast<qint64>(data.capacity());
    compSize += estimateUnorderedMapOverhead(m_compBeforeTiles);
    compSize += estimateUnorderedMapOverhead(m_compAfterTiles);
    compSize += estimateUnorderedSetOverhead(m_createdTiles);
    compSize += estimateUnorderedSetOverhead(m_removedTiles);
    m_compressedSize = compSize;
}

// ==========================================================================
//   M U L T I   T R A N S F O R M   C O M M A N D
// ==========================================================================

MultiTransformCommand::MultiTransformCommand(Canvas* canvas,
    ruwa::core::layers::LayerModel* layerModel, std::vector<TransformSnapshot>&& snapshots,
    std::optional<SelectionRestoreContext> selectionRestore)
    : m_selectionRestore(std::move(selectionRestore))
{
    m_commands.reserve(snapshots.size());
    for (auto& snapshot : snapshots) {
        if (snapshot.layerId.isNull()) {
            continue;
        }
        m_commands.push_back(std::make_unique<TransformCommand>(
            canvas, layerModel, std::move(snapshot), std::nullopt));
    }
}

MultiTransformCommand::~MultiTransformCommand() = default;

void MultiTransformCommand::undo()
{
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
        (*it)->undo();
    }
    if (m_selectionRestore) {
        applySelectionRestore(*m_selectionRestore, m_selectionRestore->before);
    }
}

void MultiTransformCommand::redo()
{
    for (auto& command : m_commands) {
        command->redo();
    }
    if (m_selectionRestore) {
        applySelectionRestore(*m_selectionRestore, m_selectionRestore->after);
    }
}

QString MultiTransformCommand::text() const
{
    return QStringLiteral("Transform Layers");
}

qint64 MultiTransformCommand::memorySize() const
{
    qint64 total = 0;
    for (const auto& command : m_commands) {
        total += command->memorySize();
    }
    return total;
}

bool MultiTransformCommand::remapForCanvasResize(
    int offsetX, int offsetY, int newWidth, int newHeight)
{
    bool ok = true;
    for (auto& command : m_commands) {
        ok = command->remapForCanvasResize(offsetX, offsetY, newWidth, newHeight) && ok;
    }
    return ok;
}

bool MultiTransformCommand::requiresAsyncPreparationForUndo() const
{
    for (const auto& command : m_commands) {
        if (command->requiresAsyncPreparationForUndo()) {
            return true;
        }
    }
    return false;
}

bool MultiTransformCommand::requiresAsyncPreparationForRedo() const
{
    for (const auto& command : m_commands) {
        if (command->requiresAsyncPreparationForRedo()) {
            return true;
        }
    }
    return false;
}

void MultiTransformCommand::prepareUndo()
{
    for (auto& command : m_commands) {
        command->prepareUndo();
    }
}

void MultiTransformCommand::prepareRedo()
{
    for (auto& command : m_commands) {
        command->prepareRedo();
    }
}

QList<QPoint> MultiTransformCommand::affectedTilePositions() const
{
    QSet<QPoint> unique;
    for (const auto& command : m_commands) {
        const QList<QPoint> points = command->affectedTilePositions();
        for (const QPoint& point : points) {
            unique.insert(point);
        }
    }

    QList<QPoint> result;
    result.reserve(unique.size());
    for (const QPoint& point : unique) {
        result.append(point);
    }
    return result;
}

} // namespace aether
