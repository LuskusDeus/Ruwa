// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   C A N V A S
// ==========================================================================

#ifndef RUWA_CORE_SCENE_CANVAS_H
#define RUWA_CORE_SCENE_CANVAS_H

#include "features/canvas/CanvasBoundsMode.h"
#include "shared/types/Types.h"
#include "shared/tiles/TileGrid.h"
#include "features/canvas/composition/CompositionCache.h"
#include "features/canvas/composition/DirtyManager.h"
#include "features/canvas/composition/TilePositionIndex.h"
#include "shared/undo/UndoManager.h"

#include <cstdint>

namespace ruwa::core::layers {
class LayerModel;
}

namespace aether {

class Canvas {
public:
    Canvas();
    Canvas(uint32_t width, uint32_t height);

    // Size (metadata — does NOT reallocate tiles)
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    void setSize(uint32_t width, uint32_t height);
    Vector2 size() const { return { static_cast<float>(m_width), static_cast<float>(m_height) }; }
    void setBoundsMode(ruwa::core::canvas::CanvasBoundsMode mode) { m_boundsMode = mode; }
    ruwa::core::canvas::CanvasBoundsMode boundsMode() const { return m_boundsMode; }
    bool isInfiniteCanvas() const;
    bool hasFiniteDocumentBounds() const;

    // Background color
    const Color& backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const Color& color);

    // Geometry helpers
    Rect bounds() const;
    Rect documentBoundsRect() const;
    Vector2 center() const;
    bool contains(const Vector2& worldPoint) const;

    // Layer model (non-owning, set externally)
    void setLayerModel(ruwa::core::layers::LayerModel* model) { m_layerModel = model; }
    ruwa::core::layers::LayerModel* layerModel() const { return m_layerModel; }

    // Composition cache — the final composited result, renderer draws from this
    CompositionCache& compositionCache() { return m_compositionCache; }
    const CompositionCache& compositionCache() const { return m_compositionCache; }

    // Dirty manager — propagates layer changes to composition cache
    DirtyManager& dirtyManager() { return m_dirtyManager; }
    const DirtyManager& dirtyManager() const { return m_dirtyManager; }

    // Tile position index — which layers have tiles at each position
    TilePositionIndex& tilePositionIndex() { return m_tilePositionIndex; }
    const TilePositionIndex& tilePositionIndex() const { return m_tilePositionIndex; }

    // Undo manager — per-document undo/redo history
    UndoManager& undoManager() { return m_undoManager; }
    const UndoManager& undoManager() const { return m_undoManager; }

    // Convenience: get composition grid for rendering
    TileGrid& compositionGrid() { return m_compositionCache.grid(); }
    const TileGrid& compositionGrid() const { return m_compositionCache.grid(); }

    // Legacy compatibility: tileGrid() returns the composition grid
    TileGrid& tileGrid() { return m_compositionCache.grid(); }
    const TileGrid& tileGrid() const { return m_compositionCache.grid(); }

private:
    uint32_t m_width = 1;
    uint32_t m_height = 1;
    ruwa::core::canvas::CanvasBoundsMode m_boundsMode
        = ruwa::core::canvas::CanvasBoundsMode::Bounded;
    Color m_backgroundColor { 1.0f, 1.0f, 1.0f, 1.0f }; // white

    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    CompositionCache m_compositionCache;
    DirtyManager m_dirtyManager;
    TilePositionIndex m_tilePositionIndex;
    UndoManager m_undoManager;
};

} // namespace aether

#endif // RUWA_CORE_SCENE_CANVAS_H
