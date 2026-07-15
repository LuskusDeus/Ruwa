// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T R A N S F O R M   C O M M A N D
// ==========================================================================

#ifndef RUWA_CORE_UNDO_TRANSFORMCOMMAND_H
#define RUWA_CORE_UNDO_TRANSFORMCOMMAND_H

#include "shared/undo/UndoManager.h"
#include "shared/undo/SelectionState.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileFormat.h"
#include "features/transform/TransformState.h"

#include <QByteArray>
#include <QList>
#include <QPoint>
#include <QUuid>

#include <atomic>
#include <optional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace ruwa::core::layers {
class LayerModel;
struct LayerData;
} // namespace ruwa::core::layers

namespace aether {

class Canvas;
class TileGrid;

// ==========================================================================
//   T R A N S F O R M   S N A P S H O T
// ==========================================================================

struct TransformSnapshot {
    QUuid layerId;

    // Pixel data of ALL tiles BEFORE transform
    std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> beforeTiles;

    // Pixel data of ALL tiles AFTER transform
    std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> afterTiles;

    // Tiles that existed before but not after
    std::unordered_set<TileKey, TileKeyHash> removedTiles;

    // Tiles that were created by the transform
    std::unordered_set<TileKey, TileKeyHash> createdTiles;

    // Tiles belong to the layer's mask grid (maskEditActive transform), so undo/
    // redo restore into maskTileGrid() and trigger a mask recomposite/thumbnail
    // refresh instead of touching the content position index.
    bool maskTarget = false;

    // Smart-layer non-destructive transform snapshot
    bool isSmartTransform = false;
    aether::TransformState beforeSmartTransform;
    aether::TransformState afterSmartTransform;
};

// ==========================================================================
//   T R A N S F O R M   C O M M A N D
// ==========================================================================

class TransformCommand : public IUndoCommand {
public:
    explicit TransformCommand(Canvas* canvas, ruwa::core::layers::LayerModel* layerModel,
        TransformSnapshot&& snapshot,
        std::optional<SelectionRestoreContext> selectionRestore = std::nullopt);
    ~TransformCommand() override;

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
    ruwa::core::layers::LayerData* resolveLayer() const;

    void applyState(const RawTileMap* rawData, const CompressedTileMap* compressedData,
        const std::unordered_set<TileKey, TileKeyHash>& tilesToRemove,
        PreparedDirection preparedDirection);
    void applyResolvedState(
        const RawTileMap& targetMap, const std::unordered_set<TileKey, TileKeyHash>& tilesToRemove);

    void compressAsync();
    void waitForCompression() const;
    void prepareCompressedState(PreparedDirection direction);

    /// Apply queued canvas-resize remaps to the compressed snapshots.
    /// Cheap no-op when nothing is pending.
    void flushPendingRemaps();

    static CompressedTileMap compressTiles(const RawTileMap& raw);

    Canvas* m_canvas;
    ruwa::core::layers::LayerModel* m_layerModel;

    QUuid m_layerId;
    std::unordered_set<TileKey, TileKeyHash> m_createdTiles;
    std::unordered_set<TileKey, TileKeyHash> m_removedTiles;

    // Raw data (available immediately, cleared after compression)
    RawTileMap m_rawBeforeTiles;
    RawTileMap m_rawAfterTiles;

    // Compressed data (populated by background thread)
    CompressedTileMap m_compBeforeTiles;
    CompressedTileMap m_compAfterTiles;

    // Async compression state
    mutable std::mutex m_mutex;
    std::future<void> m_compressFuture;
    std::atomic<bool> m_compressionDone { false };
    mutable RawTileMap m_preparedTiles;
    mutable PreparedDirection m_preparedDirection = PreparedDirection::None;

    /// Remaps queued by canvas resize, applied lazily (guarded by m_mutex)
    std::vector<PendingCanvasRemap> m_pendingRemaps;

    qint64 m_rawSize = 0;
    qint64 m_compressedSize = 0;
    /// Pixel format of the snapshotted tiles, captured from the target grid at
    /// construction (content = document format; mask target = RGBA8). Snapshot
    /// byte buffers carry no tag, so all size/interpret sites query this.
    TilePixelFormat m_contentFormat = kDefaultTileFormat;
    bool m_maskTarget = false;
    bool m_isSmartTransform = false;
    aether::TransformState m_beforeSmartTransform;
    aether::TransformState m_afterSmartTransform;
    QList<QPoint> m_affectedTilePositions;

    std::optional<SelectionRestoreContext> m_selectionRestore;
};

// ==========================================================================
//   M U L T I   T R A N S F O R M   C O M M A N D
// ==========================================================================

class MultiTransformCommand : public IUndoCommand {
public:
    explicit MultiTransformCommand(Canvas* canvas, ruwa::core::layers::LayerModel* layerModel,
        std::vector<TransformSnapshot>&& snapshots,
        std::optional<SelectionRestoreContext> selectionRestore = std::nullopt);
    ~MultiTransformCommand() override;

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

    bool empty() const { return m_commands.empty(); }

private:
    std::vector<std::unique_ptr<TransformCommand>> m_commands;
    std::optional<SelectionRestoreContext> m_selectionRestore;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_TRANSFORMCOMMAND_H
