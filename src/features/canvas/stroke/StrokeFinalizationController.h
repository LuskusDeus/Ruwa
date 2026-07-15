// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S T R O K E   F I N A L I Z A T I O N   C O N T R O L L E R
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_STROKE_STROKEFINALIZATIONCONTROLLER_H
#define RUWA_FEATURES_CANVAS_STROKE_STROKEFINALIZATIONCONTROLLER_H

#include "features/canvas/undo/DrawCommand.h"
#include "shared/undo/SelectionState.h"
#include "shared/tiles/TileTypes.h"

#include <QOpenGLFunctions_4_5_Core>

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aether {

class BrushExecutionBackend;
class Canvas;
class TileGrid;
struct TileKeyHash;

} // namespace aether
namespace ruwa::core::layers {
class LayerModel;
}
namespace aether {

// ==========================================================================
//   P E N D I N G   F I N A L I Z A T I O N
// ==========================================================================

struct PendingStrokeFinalization {
    bool active = false;

    QUuid layerId;
    bool maskTarget = false;
    std::unordered_set<TileKey, TileKeyHash> flattenedKeys;
    std::vector<TileKey> readbackKeysOrdered;

    std::unordered_map<TileKey, std::vector<uint8_t>, TileKeyHash> beforeTiles;
    std::unordered_set<TileKey, TileKeyHash> createdTiles;
    std::unordered_set<TileKey, TileKeyHash> removedTiles;

    bool eraseMode = false;

    GLsync fence = nullptr;
};

// ==========================================================================
//   S T R O K E   F I N A L I Z A T I O N   C O N T R O L L E R
// ==========================================================================

class StrokeFinalizationController {
public:
    struct Context {
        std::function<TileGrid*()> getActiveLayerGrid;
        std::function<BrushExecutionBackend*()> getBrushExecutionBackend;
        std::function<void()> makeCurrent;
        std::function<void()> doneCurrent;
        Canvas* canvas = nullptr;
        ruwa::core::layers::LayerModel* layerModel = nullptr;

        /// Optional: selection state to restore on undo/redo
        std::optional<SelectionRestoreContext> selectionRestore;
    };

    /// Consumes pending, performs readback, builds snapshot, pushes undo.
    /// Clears pending when done.
    static void finalize(PendingStrokeFinalization& pending, Context& ctx);
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_STROKE_STROKEFINALIZATIONCONTROLLER_H
