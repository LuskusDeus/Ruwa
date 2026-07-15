// SPDX-License-Identifier: MPL-2.0

#ifndef AETHER_PAINT_GL_CAMERA_FRAME_STATE_H
#define AETHER_PAINT_GL_CAMERA_FRAME_STATE_H

#include "shared/types/Types.h"

#include <cstdint>
#include <unordered_map>

namespace aether {

class OpenGLCanvasWidget;

struct PaintGLCameraFrameState {
    bool valid = false;
    Vector2 position {};
    float zoom = 0.0f;
    float rotation = 0.0f;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    bool compositionCacheClean = false;
};

struct PaintGLCompositeContext {
    bool pureCameraFrame = false;
    bool previousFrameCompositionCacheClean = false;
};

std::unordered_map<const OpenGLCanvasWidget*, PaintGLCameraFrameState>& paintGLCameraFrameStates();
std::unordered_map<const OpenGLCanvasWidget*, PaintGLCompositeContext>& paintGLCompositeContexts();
PaintGLCameraFrameState capturePaintGLCameraFrameState(const OpenGLCanvasWidget* widget);
bool paintGLCameraStateChanged(
    const PaintGLCameraFrameState& previous, const PaintGLCameraFrameState& current);

} // namespace aether

#endif // AETHER_PAINT_GL_CAMERA_FRAME_STATE_H
