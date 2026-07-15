// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   D R A W   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_DRAWCOMMAND_H
#define RUWA_CORE_UNDO_DRAWCOMMAND_H

#include "shared/undo/UndoManager.h"
#include "shared/undo/SelectionState.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileFormat.h"

#include <QByteArray>
#include <QList>
#include <QPoint>
#include <QUuid>

#include <atomic>
#include <optional>
#include <future>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

class Canvas;
class TileGrid;

// ==========================================================================
//   S T R O K E   S N A P S H O T   (raw -- captured during stroke)
// ==========================================================================

struct StrokeSnapshot {
    QUuid layerId;

    /// When true the stroke targeted the layer's mask grid, not its pixels.
    bool maskTarget = false;

    /// Pixel data of affected tiles BEFORE flatten (raw, TILE_BYTE_SIZE each)
    std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> beforeTiles;

    /// Pixel data of affected tiles AFTER flatten (raw, TILE_BYTE_SIZE each)
    std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> afterTiles;

    /// Tiles that did not exist before the stroke (allocated by brush)
    std::unordered_set<TileKey, TileKeyHash> createdTiles;

    /// Tiles deleted by pruneEmpty() after erase stroke
    std::unordered_set<TileKey, TileKeyHash> removedTiles;
};

// ==========================================================================
//   D R A W   C O M M A N D
// ==========================================================================
//
//   Stores tile snapshots. Compression is deferred to a background thread
//   so that endStroke() returns immediately without blocking the UI.
//
//   While compression is in progress, undo/redo uses the raw pixel data
//   directly. Once compression finishes, raw data is released and the
//   compressed form is used (with on-the-fly decompression).
//

class DrawCommand : public IUndoCommand {
public:
    explicit DrawCommand(Canvas* canvas, ruwa::core::layers::LayerModel* layerModel,
        StrokeSnapshot&& snapshot,
        std::optional<SelectionRestoreContext> selectionRestore = std::nullopt);

    ~DrawCommand() override;

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;
    bool remapForCanvasResize(int offsetX, int offsetY, int newWidth, int newHeight) override;
    bool requiresAsyncPreparationForUndo() const override;
    bool requiresAsyncPreparationForRedo() const override;
    void prepareUndo() override;
    void prepareRedo() override;
    QList<QPoint> affectedTilePositions() const override;

private:
    using RawTileMap = std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash>;
    using CompressedTileMap = std::unordered_map<TileKey, QByteArray, TileKeyHash>;
    enum class PreparedDirection { None, Undo, Redo };

    /// Canvas-resize remap deferred until the command is actually needed.
    struct PendingCanvasRemap {
        int offsetX = 0;
        int offsetY = 0;
        int newWidth = 0;
        int newHeight = 0;
    };

    TileGrid* resolveGrid() const;

    /// Apply a tile state (handles both raw and compressed storage)
    void applyState(const RawTileMap* rawData, const CompressedTileMap* compressedData,
        PreparedDirection preparedDirection);
    void applyResolvedState(const RawTileMap& targetMap);

    /// Background compression task
    void compressAsync();

    /// Ensure compression is finished (blocks if still running)
    void waitForCompression() const;
    void prepareCompressedState(PreparedDirection direction);

    /// Apply queued canvas-resize remaps to the compressed snapshots.
    /// Cheap no-op when nothing is pending.
    void flushPendingRemaps();

    /// Compress raw tile map -> compressed tile map
    static CompressedTileMap compressTiles(const RawTileMap& raw);

    Canvas* m_canvas;
    ruwa::core::layers::LayerModel* m_layerModel;

    QUuid m_layerId;
    bool m_maskTarget = false;
    /// Pixel format of the snapshotted tiles, captured from the target grid at
    /// construction. Content grids follow the document format; mask targets are
    /// RGBA8. RawTileMap/compressed buffers carry no tag, so every size/interpret
    /// site queries this instead of the global knob.
    TilePixelFormat m_contentFormat = kDefaultTileFormat;
    std::unordered_set<TileKey, TileKeyHash> m_createdTiles;
    std::unordered_set<TileKey, TileKeyHash> m_removedTiles;

    // --- Raw data (available immediately, cleared after compression) ---
    RawTileMap m_rawBeforeTiles;
    RawTileMap m_rawAfterTiles;

    // --- Compressed data (populated by background thread) ---
    CompressedTileMap m_compBeforeTiles;
    CompressedTileMap m_compAfterTiles;

    // --- Async compression state ---
    mutable std::mutex m_mutex;
    std::future<void> m_compressFuture;
    std::atomic<bool> m_compressionDone { false };
    mutable RawTileMap m_preparedTiles;
    mutable PreparedDirection m_preparedDirection = PreparedDirection::None;

    /// Remaps queued by canvas resize, applied lazily (guarded by m_mutex)
    std::vector<PendingCanvasRemap> m_pendingRemaps;

    qint64 m_rawSize = 0; // Size before compression
    qint64 m_compressedSize = 0; // Size after compression

    std::optional<SelectionRestoreContext> m_selectionRestore;
    QList<QPoint> m_affectedTilePositions;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_DRAWCOMMAND_H
