// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   O V E R L A Y   M A N A G E R
// ==========================================================================

#ifndef RUWA_CANVAS_OVERLAYS_CANVASOVERLAYMANAGER_H
#define RUWA_CANVAS_OVERLAYS_CANVASOVERLAYMANAGER_H

#include "shared/types/Result.h"

#include <QOpenGLFunctions_4_5_Core>
#include <memory>

namespace aether {

class TransformOverlay;
class CanvasResizeOverlayGL;
class BrushCursorOverlayGL;
class EyedropperCursorOverlayGL;
class LassoOverlay;
class LassoFillOverlay;
class TextEditOverlayGL;

/**
 * @brief Centralized manager for all GL overlays rendered on the canvas.
 *
 * Owns and initializes: TransformOverlay, CanvasResizeOverlayGL,
 * BrushCursorOverlayGL, EyedropperCursorOverlayGL, LassoOverlay, LassoFillOverlay.
 */
class CanvasOverlayManager {
public:
    CanvasOverlayManager();
    ~CanvasOverlayManager();

    CanvasOverlayManager(const CanvasOverlayManager&) = delete;
    CanvasOverlayManager& operator=(const CanvasOverlayManager&) = delete;

    Result<void> initialize(QOpenGLFunctions_4_5_Core* gl);
    void shutdown();

    TransformOverlay* transformOverlay() const;
    CanvasResizeOverlayGL* canvasResizeOverlay() const;
    BrushCursorOverlayGL* brushCursorOverlay() const;
    EyedropperCursorOverlayGL* eyedropperCursorOverlay() const;
    LassoOverlay* lassoOverlay() const;
    LassoFillOverlay* lassoFillOverlay() const;
    TextEditOverlayGL* textEditOverlay() const;

private:
    std::unique_ptr<TransformOverlay> m_transformOverlay;
    std::unique_ptr<CanvasResizeOverlayGL> m_canvasResizeOverlay;
    std::unique_ptr<BrushCursorOverlayGL> m_brushCursorOverlay;
    std::unique_ptr<EyedropperCursorOverlayGL> m_eyedropperCursorOverlay;
    std::unique_ptr<LassoOverlay> m_lassoOverlay;
    std::unique_ptr<LassoFillOverlay> m_lassoFillOverlay;
    std::unique_ptr<TextEditOverlayGL> m_textEditOverlay;
};

} // namespace aether

#endif // RUWA_CANVAS_OVERLAYS_CANVASOVERLAYMANAGER_H
