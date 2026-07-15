// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   D R A W   C O M M A N D
// ==========================================================================

#include "features/canvas/undo/DrawCommand.h"
#include "shared/undo/SelectionState.h"

#include "features/canvas/scene/Canvas.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileData.h"
#include "shared/tiles/TilePixelAccess.h"
#include "features/layers/model/LayerModel.h"
#include "features/layers/model/LayerData.h"
#include <QSemaphore>
#include <QSet>
#include <algorithm>
#include <cstring>
#include <chrono>

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

constexpr int kMaxConcurrentStrokeCompressionJobs = 2;

QSemaphore& strokeCompressionLimiter()
{
    static QSemaphore limiter(kMaxConcurrentStrokeCompressionJobs);
    return limiter;
}

class ScopedSemaphoreAcquire {
public:
    explicit ScopedSemaphoreAcquire(QSemaphore& semaphore)
        : m_semaphore(semaphore)
    {
        m_semaphore.acquire();
    }

    ~ScopedSemaphoreAcquire() { m_semaphore.release(); }

    ScopedSemaphoreAcquire(const ScopedSemaphoreAcquire&) = delete;
    ScopedSemaphoreAcquire& operator=(const ScopedSemaphoreAcquire&) = delete;

private:
    QSemaphore& m_semaphore;
};

QList<QPoint> buildAffectedTilePositions(const std::unordered_set<TileKey, TileKeyHash>& beforeKeys,
    const std::unordered_set<TileKey, TileKeyHash>& afterKeys,
    const std::unordered_set<TileKey, TileKeyHash>& createdTiles,
    const std::unordered_set<TileKey, TileKeyHash>& removedTiles);

QList<QPoint> buildAffectedTilePositions(const RawTileMap& beforeTiles,
    const RawTileMap& afterTiles, const std::unordered_set<TileKey, TileKeyHash>& createdTiles,
    const std::unordered_set<TileKey, TileKeyHash>& removedTiles)
{
    std::unordered_set<TileKey, TileKeyHash> beforeKeys;
    beforeKeys.reserve(beforeTiles.size());
    for (const auto& [key, _] : beforeTiles) {
        beforeKeys.insert(key);
    }

    std::unordered_set<TileKey, TileKeyHash> afterKeys;
    afterKeys.reserve(afterTiles.size());
    for (const auto& [key, _] : afterTiles) {
        afterKeys.insert(key);
    }

    return buildAffectedTilePositions(beforeKeys, afterKeys, createdTiles, removedTiles);
}

QList<QPoint> buildAffectedTilePositions(const std::unordered_set<TileKey, TileKeyHash>& beforeKeys,
    const std::unordered_set<TileKey, TileKeyHash>& afterKeys,
    const std::unordered_set<TileKey, TileKeyHash>& createdTiles,
    const std::unordered_set<TileKey, TileKeyHash>& removedTiles)
{
    QSet<QPoint> uniqueTiles;
    uniqueTiles.reserve(static_cast<qsizetype>(
        beforeKeys.size() + afterKeys.size() + createdTiles.size() + removedTiles.size()));

    const auto addKey
        = [&uniqueTiles](const TileKey& key) { uniqueTiles.insert(QPoint(key.x, key.y)); };

    for (const TileKey& key : beforeKeys) {
        addKey(key);
    }
    for (const TileKey& key : afterKeys) {
        addKey(key);
    }
    for (const TileKey& key : createdTiles) {
        addKey(key);
    }
    for (const TileKey& key : removedTiles) {
        addKey(key);
    }

    return uniqueTiles.values();
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
    RawTileMap dst;
    dst.reserve(src.size());
    // Snapshots are stored/compressed in the owning command's content format;
    // uncompressed payload must match its byte size, not the 8-bit constant.
    const size_t bytesPerTile = tileByteSize(fmt);
    for (const auto& [key, compData] : src) {
        const QByteArray raw = qUncompress(compData);
        if (static_cast<size_t>(raw.size()) != bytesPerTile)
            continue;
        std::vector<uint8_t> pixels(bytesPerTile);
        std::memcpy(pixels.data(), raw.constData(), bytesPerTile);
        dst.emplace(key, std::move(pixels));
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

DrawCommand::DrawCommand(Canvas* canvas, ruwa::core::layers::LayerModel* layerModel,
    StrokeSnapshot&& snapshot, std::optional<SelectionRestoreContext> selectionRestore)
    : m_canvas(canvas)
    , m_layerModel(layerModel)
    , m_layerId(snapshot.layerId)
    , m_maskTarget(snapshot.maskTarget)
    , m_createdTiles(std::move(snapshot.createdTiles))
    , m_removedTiles(std::move(snapshot.removedTiles))
    , m_rawBeforeTiles(std::move(snapshot.beforeTiles))
    , m_rawAfterTiles(std::move(snapshot.afterTiles))
    , m_selectionRestore(std::move(selectionRestore))
{
    // Capture the snapshot's pixel format (single source of truth for the
    // untagged byte buffers' size/interpretation). Mask grids are ALWAYS RGBA8 by
    // invariant, so pin that directly rather than reading the grid — the grid may
    // already be detached at construction time (e.g. ApplyMaskCommand builds the
    // mask DrawCommand after clearMask()), which would otherwise fall back to the
    // kDefaultTileFormat member and mis-size the RGBA8 mask buffers. Content
    // follows the document format read from the live grid.
    if (m_maskTarget) {
        m_contentFormat = TilePixelFormat::RGBA8;
    } else if (const TileGrid* grid = resolveGrid()) {
        m_contentFormat = grid->format();
    }

    m_affectedTilePositions = buildAffectedTilePositions(
        m_rawBeforeTiles, m_rawAfterTiles, m_createdTiles, m_removedTiles);

    // Estimate raw memory footprint using capacities + container overhead.
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

DrawCommand::~DrawCommand()
{
    // Ensure background thread finishes before destroying members
    waitForCompression();
}

// ==========================================================================
//   B A C K G R O U N D   C O M P R E S S I O N
// ==========================================================================

void DrawCommand::compressAsync()
{
    ScopedSemaphoreAcquire compressionSlot(strokeCompressionLimiter());

    // Compress into local variables (no lock needed — we're the only writer)
    // Use compression level 1 for speed (still ~50-80% savings on tile data)
    auto compBefore = compressTiles(m_rawBeforeTiles);
    auto compAfter = compressTiles(m_rawAfterTiles);

    // Estimate compressed memory footprint (capacity + container overhead).
    qint64 compSize = 0;
    for (const auto& [key, data] : compBefore)
        compSize += static_cast<qint64>(data.capacity());
    for (const auto& [key, data] : compAfter)
        compSize += static_cast<qint64>(data.capacity());

    compSize += estimateUnorderedMapOverhead(compBefore);
    compSize += estimateUnorderedMapOverhead(compAfter);
    compSize += estimateUnorderedSetOverhead(m_createdTiles);
    compSize += estimateUnorderedSetOverhead(m_removedTiles);

    // Swap in compressed data, release raw data
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

void DrawCommand::waitForCompression() const
{
    if (m_compressFuture.valid()) {
        // const_cast needed because std::future::wait() is not const
        const_cast<std::future<void>&>(m_compressFuture).wait();
    }
}

DrawCommand::CompressedTileMap DrawCommand::compressTiles(const RawTileMap& raw)
{
    CompressedTileMap result;
    result.reserve(raw.size());
    for (const auto& [key, pixels] : raw) {
        QByteArray rawBytes(
            reinterpret_cast<const char*>(pixels.data()), static_cast<qsizetype>(pixels.size()));
        result[key] = qCompress(rawBytes, 1);
    }
    return result;
}

// ==========================================================================
//   U N D O  /  R E D O
// ==========================================================================

void DrawCommand::undo()
{
    using Clock = std::chrono::high_resolution_clock;
    using Us = std::chrono::microseconds;

    flushPendingRemaps();

    const auto t0 = Clock::now();
    applyState(&m_rawBeforeTiles, &m_compBeforeTiles, PreparedDirection::Undo);
    const auto t1 = Clock::now();

    if (m_selectionRestore) {
        applySelectionRestore(*m_selectionRestore, m_selectionRestore->before);
    }
    const auto t2 = Clock::now();

    const qint64 applyUs = std::chrono::duration_cast<Us>(t1 - t0).count();
    const qint64 selUs = std::chrono::duration_cast<Us>(t2 - t1).count();
    const qint64 totalUs = std::chrono::duration_cast<Us>(t2 - t0).count();
}

void DrawCommand::redo()
{
    flushPendingRemaps();
    applyState(&m_rawAfterTiles, &m_compAfterTiles, PreparedDirection::Redo);
    if (m_selectionRestore) {
        applySelectionRestore(*m_selectionRestore, m_selectionRestore->after);
    }
}

// ==========================================================================
//   A P P L Y   S T A T E
// ==========================================================================

void DrawCommand::applyState(const RawTileMap* rawData, const CompressedTileMap* compressedData,
    PreparedDirection preparedDirection)
{
    using Clock = std::chrono::high_resolution_clock;
    using Us = std::chrono::microseconds;

    const auto tGridStart = Clock::now();
    TileGrid* grid = resolveGrid();
    const auto tGridEnd = Clock::now();
    if (!grid)
        return;

    RawTileMap decompressedTarget;
    const RawTileMap* targetMap = rawData;

    qint64 lockAcquireUs = 0;
    qint64 decompressUs = 0;
    bool wasCompressed = false;
    bool usedPrepared = false;
    size_t sourceTiles = 0;

    {
        const auto tLock0 = Clock::now();
        std::lock_guard<std::mutex> lock(m_mutex);
        lockAcquireUs = std::chrono::duration_cast<Us>(Clock::now() - tLock0).count();

        wasCompressed = m_compressionDone.load(std::memory_order_acquire);
        if (wasCompressed) {
            sourceTiles = compressedData->size();
            if (m_preparedDirection == preparedDirection) {
                targetMap = &m_preparedTiles;
                usedPrepared = true;
            } else {
                const auto tD0 = Clock::now();
                decompressedTarget = decompressTiles(*compressedData, m_contentFormat);
                decompressUs = std::chrono::duration_cast<Us>(Clock::now() - tD0).count();
                targetMap = &decompressedTarget;
            }
        } else {
            sourceTiles = rawData->size();
            // Compression is still running: compressAsync() frees the raw
            // tile maps under m_mutex the moment it finishes, so applying
            // from the raw snapshot must complete WITHOUT releasing the
            // lock. Releasing it here and reading rawData afterwards races
            // the free — observed as an undo-right-after-stroke crash with
            // memcpy reading a freed vector. applyResolvedState only talks
            // to the tile grid / dirty manager, which never take m_mutex,
            // so holding it across the call cannot deadlock.
            applyResolvedState(*rawData);
            return;
        }
    }

    applyResolvedState(*targetMap);
}

void DrawCommand::applyResolvedState(const RawTileMap& targetMap)
{
    using Clock = std::chrono::high_resolution_clock;
    using Us = std::chrono::microseconds;

    const auto tGridStart = Clock::now();
    TileGrid* grid = resolveGrid();
    const auto tGridEnd = Clock::now();
    if (!grid) {
        return;
    }

    std::unordered_set<TileKey, TileKeyHash> affected;
    // Format-sized opaque transport: at RGBA8 bytesPerTile == TILE_BYTE_SIZE
    // (unchanged); wider formats move the full per-pixel payload. The all-zero
    // comparison is valid for any format (zero bytes == transparent everywhere).
    const size_t bytesPerTile = tileByteSize(grid->format());
    static const std::vector<uint8_t> kZeroTile(tileMaxByteSize(), 0);

    int skippedSamePixels = 0;
    int skippedZeroNoTile = 0;
    int writtenTiles = 0;
    int removedZeroTiles = 0;
    int createdNewTiles = 0;

    const auto tLoopStart = Clock::now();

    for (const auto& [key, targetPixels] : targetMap) {
        if (targetPixels.size() != bytesPerTile)
            continue;

        const uint8_t* targetBytes = targetPixels.data();

        TileData* tile = grid->getTile(key);
        const bool targetIsZero = std::memcmp(targetBytes, kZeroTile.data(), bytesPerTile) == 0;

        if (tile) {
            if (std::memcmp(tile->pixels(), targetBytes, bytesPerTile) == 0) {
                ++skippedSamePixels;
                continue;
            }
        } else {
            if (targetIsZero) {
                ++skippedZeroNoTile;
                continue;
            }
            tile = &grid->getOrCreateTile(key);
            ++createdNewTiles;
        }

        std::memcpy(tile->pixels(), targetBytes, bytesPerTile);

        tile->markDirty();
        grid->markDirty(key);

        if (targetIsZero) {
            grid->removeTile(key);
            if (!m_maskTarget) {
                m_canvas->tilePositionIndex().removeEntry(key, m_layerId);
            }
            ++removedZeroTiles;
        } else {
            if (!m_maskTarget) {
                m_canvas->tilePositionIndex().addEntry(key, m_layerId);
            }
        }
        ++writtenTiles;
        affected.insert(key);
    }

    const auto tLoopEnd = Clock::now();

    // When restoring "before" state (undo), tiles in m_createdTiles did not exist
    // before the fill. They are not in targetMap, so we must remove them explicitly.
    int explicitlyRemovedCreated = 0;
    const auto tCreatedStart = Clock::now();

    for (const TileKey& key : m_createdTiles) {
        if (targetMap.count(key))
            continue; // Already handled above
        TileData* tile = grid->getTile(key);
        if (!tile)
            continue;
        grid->removeTile(key);
        if (!m_maskTarget) {
            m_canvas->tilePositionIndex().removeEntry(key, m_layerId);
        }
        grid->markDirty(key);
        affected.insert(key);
        ++explicitlyRemovedCreated;
    }

    const auto tCreatedEnd = Clock::now();

    const auto tDirtyStart = Clock::now();
    if (!affected.empty()) {
        m_canvas->dirtyManager().onTilesDirtied(m_layerId, affected);
    }
    if (!affected.empty() && m_maskTarget && m_layerModel) {
        if (auto* layer = m_layerModel->layerById(m_layerId)) {
            layer->maskThumbnailDirty = true;
        }
        m_layerModel->notifyLayerDataChanged(m_layerId);
    }
    const auto tDirtyEnd = Clock::now();
}

// ==========================================================================
//   H E L P E R S
// ==========================================================================

TileGrid* DrawCommand::resolveGrid() const
{
    if (!m_layerModel)
        return nullptr;

    TileGrid* result = nullptr;
    m_layerModel->forEach([&](ruwa::core::layers::LayerData* layer) {
        if (layer->id != m_layerId) {
            return;
        }
        if (m_maskTarget) {
            if (layer->maskGrid) {
                result = layer->maskGrid.get();
            }
        } else if (layer->isRaster() && layer->tileGrid) {
            result = layer->tileGrid.get();
        }
    });
    return result;
}

QString DrawCommand::text() const
{
    return QStringLiteral("Brush Stroke");
}

qint64 DrawCommand::memorySize() const
{
    if (m_compressionDone.load(std::memory_order_acquire)) {
        return m_compressedSize;
    }
    return m_rawSize;
}

bool DrawCommand::requiresAsyncPreparationForUndo() const
{
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

bool DrawCommand::requiresAsyncPreparationForRedo() const
{
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

void DrawCommand::prepareUndo()
{
    prepareCompressedState(PreparedDirection::Undo);
}

void DrawCommand::prepareRedo()
{
    prepareCompressedState(PreparedDirection::Redo);
}

void DrawCommand::prepareCompressedState(PreparedDirection direction)
{
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

QList<QPoint> DrawCommand::affectedTilePositions() const
{
    return m_affectedTilePositions;
}

bool DrawCommand::remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight)
{
    // O(1): the heavy tile remap is deferred until this command is actually
    // needed (undo/redo/prefetch), so a canvas resize no longer scales with
    // undo-stack depth.
    std::lock_guard<std::mutex> lock(m_mutex);
    RawTileMap {}.swap(m_preparedTiles);
    m_preparedDirection = PreparedDirection::None;
    m_pendingRemaps.push_back({ offsetX, offsetY, newWidth, newHeight });
    return true;
}

void DrawCommand::flushPendingRemaps()
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
    m_affectedTilePositions
        = buildAffectedTilePositions(beforeKeys, afterKeys, m_createdTiles, m_removedTiles);

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

} // namespace aether
