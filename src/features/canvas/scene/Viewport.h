// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   V I E W P O R T
// ==========================================================================

#ifndef RUWA_CORE_SCENE_VIEWPORT_H
#define RUWA_CORE_SCENE_VIEWPORT_H

#include "shared/types/Types.h"
#include "features/canvas/scene/Camera2D.h"

#include <memory>

namespace aether {

// ==========================================================================
//   V I E W P O R T
// ==========================================================================

/**
 * @brief Viewport manages the rendering area and camera
 *
 * Combines render target dimensions with camera to provide
 * complete view transformation for rendering.
 */
class Viewport {
public:
    Viewport();
    Viewport(uint32_t width, uint32_t height);

    // Dimensions
    void resize(uint32_t width, uint32_t height);
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    Extent2D extent() const { return { m_width, m_height }; }
    Vector2 size() const { return { static_cast<float>(m_width), static_cast<float>(m_height) }; }
    float aspectRatio() const;

    // Camera access
    Camera2D& camera() { return m_camera; }
    const Camera2D& camera() const { return m_camera; }

    // Viewport rectangle in NDC
    Rect ndcRect() const { return { -1.0f, -1.0f, 2.0f, 2.0f }; }

    // Transform matrices (delegated to camera)
    std::array<float, 16> viewMatrix() const { return m_camera.viewMatrix(); }
    std::array<float, 16> projectionMatrix() const;
    std::array<float, 16> viewProjectionMatrix() const;

    // Coordinate transformations
    Vector2 screenToWorld(const Vector2& screenPos) const;
    Vector2 worldToScreen(const Vector2& worldPos) const;

    // Check if point is within viewport bounds
    bool containsScreenPoint(const Vector2& screenPos) const;
    bool containsScreenPoint(float x, float y) const;

private:
    uint32_t m_width = 1;
    uint32_t m_height = 1;
    Camera2D m_camera;
};

} // namespace aether

#endif // RUWA_CORE_SCENE_VIEWPORT_H
