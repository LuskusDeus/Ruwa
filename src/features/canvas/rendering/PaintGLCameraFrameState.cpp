// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/PaintGLCameraFrameState.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/canvas/scene/Camera2D.h"
#include "features/canvas/scene/Viewport.h"

#include <algorithm>
#include <cmath>

namespace aether {

std::unordered_map<const aether::OpenGLCanvasWidget*, PaintGLCameraFrameState>&
paintGLCameraFrameStates()
{
    static std::unordered_map<const aether::OpenGLCanvasWidget*, PaintGLCameraFrameState> states;
    return states;
}

std::unordered_map<const aether::OpenGLCanvasWidget*, PaintGLCompositeContext>&
paintGLCompositeContexts()
{
    static std::unordered_map<const aether::OpenGLCanvasWidget*, PaintGLCompositeContext> contexts;
    return contexts;
}

PaintGLCameraFrameState capturePaintGLCameraFrameState(const aether::OpenGLCanvasWidget* widget)
{
    PaintGLCameraFrameState state;
    if (!widget) {
        return state;
    }

    const auto& camera = widget->viewport().camera();
    state.valid = true;
    state.position = camera.position();
    state.zoom = static_cast<float>(camera.zoom());
    state.rotation = static_cast<float>(camera.rotation());
    state.viewportWidth = static_cast<uint32_t>(std::max(0, widget->width()));
    state.viewportHeight = static_cast<uint32_t>(std::max(0, widget->height()));
    return state;
}

bool paintGLCameraStateChanged(
    const PaintGLCameraFrameState& previous, const PaintGLCameraFrameState& current)
{
    if (!previous.valid || !current.valid) {
        return false;
    }

    constexpr float kCameraEpsilon = 0.01f;
    return previous.viewportWidth != current.viewportWidth
        || previous.viewportHeight != current.viewportHeight
        || std::abs(previous.position.x - current.position.x) > kCameraEpsilon
        || std::abs(previous.position.y - current.position.y) > kCameraEpsilon
        || std::abs(previous.zoom - current.zoom) > kCameraEpsilon
        || std::abs(previous.rotation - current.rotation) > kCameraEpsilon;
}

} // namespace aether
