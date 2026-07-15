// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   O V E R L A Y   M A N A G E R
// ==========================================================================

#include "features/canvas/overlays/CanvasOverlayManager.h"
#include "features/canvas/overlays/TransformOverlay.h"
#include "features/canvas/overlays/CanvasResizeOverlayGL.h"
#include "features/canvas/overlays/BrushCursorOverlayGL.h"
#include "features/canvas/overlays/EyedropperCursorOverlayGL.h"
#include "features/canvas/overlays/LassoOverlay.h"
#include "features/canvas/overlays/LassoFillOverlay.h"
#include "features/canvas/overlays/TextEditOverlayGL.h"
namespace aether {

CanvasOverlayManager::CanvasOverlayManager() = default;

CanvasOverlayManager::~CanvasOverlayManager()
{
    shutdown();
}

Result<void> CanvasOverlayManager::initialize(QOpenGLFunctions_4_5_Core* gl)
{
    if (!gl) {
        return { ErrorCode::InvalidArgument, "CanvasOverlayManager: null GL context" };
    }

    m_transformOverlay = std::make_unique<TransformOverlay>(gl);
    auto overlayResult = m_transformOverlay->initialize();
    if (!overlayResult) { }

    m_canvasResizeOverlay = std::make_unique<CanvasResizeOverlayGL>(gl);
    auto resizeOverlayResult = m_canvasResizeOverlay->initialize();
    if (!resizeOverlayResult) { }

    m_brushCursorOverlay = std::make_unique<BrushCursorOverlayGL>(gl);
    auto brushCursorResult = m_brushCursorOverlay->initialize();
    if (!brushCursorResult) { }

    m_eyedropperCursorOverlay = std::make_unique<EyedropperCursorOverlayGL>(gl);
    auto eyedropperResult = m_eyedropperCursorOverlay->initialize();
    if (!eyedropperResult) { }

    m_lassoOverlay = std::make_unique<LassoOverlay>(gl);
    auto lassoResult = m_lassoOverlay->initialize();
    if (!lassoResult) { }

    m_lassoFillOverlay = std::make_unique<LassoFillOverlay>(gl);
    auto lassoFillResult = m_lassoFillOverlay->initialize();
    if (!lassoFillResult) { }

    m_textEditOverlay = std::make_unique<TextEditOverlayGL>(gl);
    auto textEditResult = m_textEditOverlay->initialize();
    if (!textEditResult) { }

    return Result<void>::ok();
}

void CanvasOverlayManager::shutdown()
{
    m_textEditOverlay.reset();
    m_lassoFillOverlay.reset();
    m_lassoOverlay.reset();
    m_eyedropperCursorOverlay.reset();
    m_brushCursorOverlay.reset();
    m_canvasResizeOverlay.reset();
    m_transformOverlay.reset();
}

TransformOverlay* CanvasOverlayManager::transformOverlay() const
{
    return m_transformOverlay.get();
}

CanvasResizeOverlayGL* CanvasOverlayManager::canvasResizeOverlay() const
{
    return m_canvasResizeOverlay.get();
}

BrushCursorOverlayGL* CanvasOverlayManager::brushCursorOverlay() const
{
    return m_brushCursorOverlay.get();
}

EyedropperCursorOverlayGL* CanvasOverlayManager::eyedropperCursorOverlay() const
{
    return m_eyedropperCursorOverlay.get();
}

LassoOverlay* CanvasOverlayManager::lassoOverlay() const
{
    return m_lassoOverlay.get();
}

LassoFillOverlay* CanvasOverlayManager::lassoFillOverlay() const
{
    return m_lassoFillOverlay.get();
}

TextEditOverlayGL* CanvasOverlayManager::textEditOverlay() const
{
    return m_textEditOverlay.get();
}

} // namespace aether
