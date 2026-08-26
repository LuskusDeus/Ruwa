// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   O V E R L A Y   M A N A G E R
// ==========================================================================

#include "features/canvas/overlays/CanvasOverlayManager.h"
#include "features/canvas/overlays/TransformOverlay.h"
#include "features/canvas/overlays/CanvasResizeOverlayGL.h"
#include "features/canvas/overlays/BrushCursorOverlayGL.h"
#include "features/canvas/overlays/EyedropperCursorOverlayGL.h"
#include "features/canvas/overlays/ToolCursorOverlayGL.h"
#include "features/canvas/overlays/LassoOverlay.h"
#include "features/canvas/overlays/LassoFillOverlay.h"
#include "features/canvas/overlays/TextEditOverlayGL.h"

#include <string>
#include <utility>

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

    ErrorCode firstErrorCode = ErrorCode::None;
    std::string errors;
    const auto recordError
        = [&firstErrorCode, &errors](const Result<void>& result, const char* overlayName) {
              if (result) {
                  return;
              }
              if (firstErrorCode == ErrorCode::None) {
                  firstErrorCode = result.error().code == ErrorCode::None
                      ? ErrorCode::ShaderCompilationFailed
                      : result.error().code;
              }
              if (!errors.empty()) {
                  errors += '\n';
              }
              errors += std::string("Failed to initialize ") + overlayName + ": "
                  + result.error().message;
          };

    m_transformOverlay = std::make_unique<TransformOverlay>(gl);
    auto overlayResult = m_transformOverlay->initialize();
    recordError(overlayResult, "transform overlay");

    m_canvasResizeOverlay = std::make_unique<CanvasResizeOverlayGL>(gl);
    auto resizeOverlayResult = m_canvasResizeOverlay->initialize();
    recordError(resizeOverlayResult, "canvas resize overlay");

    m_brushCursorOverlay = std::make_unique<BrushCursorOverlayGL>(gl);
    auto brushCursorResult = m_brushCursorOverlay->initialize();
    recordError(brushCursorResult, "brush cursor overlay");

    m_eyedropperCursorOverlay = std::make_unique<EyedropperCursorOverlayGL>(gl);
    auto eyedropperResult = m_eyedropperCursorOverlay->initialize();
    recordError(eyedropperResult, "eyedropper cursor overlay");

    m_toolCursorOverlay = std::make_unique<ToolCursorOverlayGL>(gl);
    auto toolCursorResult = m_toolCursorOverlay->initialize();
    recordError(toolCursorResult, "tool cursor overlay");

    m_lassoOverlay = std::make_unique<LassoOverlay>(gl);
    auto lassoResult = m_lassoOverlay->initialize();
    recordError(lassoResult, "lasso overlay");

    m_lassoFillOverlay = std::make_unique<LassoFillOverlay>(gl);
    auto lassoFillResult = m_lassoFillOverlay->initialize();
    recordError(lassoFillResult, "lasso fill overlay");

    m_textEditOverlay = std::make_unique<TextEditOverlayGL>(gl);
    auto textEditResult = m_textEditOverlay->initialize();
    recordError(textEditResult, "text edit overlay");

    if (firstErrorCode != ErrorCode::None) {
        return { firstErrorCode, std::move(errors) };
    }

    return Result<void>::ok();
}

void CanvasOverlayManager::shutdown()
{
    m_textEditOverlay.reset();
    m_lassoFillOverlay.reset();
    m_lassoOverlay.reset();
    m_toolCursorOverlay.reset();
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

ToolCursorOverlayGL* CanvasOverlayManager::toolCursorOverlay() const
{
    return m_toolCursorOverlay.get();
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
