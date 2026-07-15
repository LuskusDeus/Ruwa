// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   V I E W P O R T
// ==========================================================================

#include "features/canvas/scene/Viewport.h"

namespace aether {

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

Viewport::Viewport() = default;

Viewport::Viewport(uint32_t width, uint32_t height)
    : m_width(width > 0 ? width : 1)
    , m_height(height > 0 ? height : 1)
{
}

// ==========================================================================
//   D I M E N S I O N S
// ==========================================================================

void Viewport::resize(uint32_t width, uint32_t height)
{
    m_width = width > 0 ? width : 1;
    m_height = height > 0 ? height : 1;
}

float Viewport::aspectRatio() const
{
    return static_cast<float>(m_width) / static_cast<float>(m_height);
}

// ==========================================================================
//   T R A N S F O R M   M A T R I C E S
// ==========================================================================

std::array<float, 16> Viewport::projectionMatrix() const
{
    return m_camera.projectionMatrix(static_cast<float>(m_width), static_cast<float>(m_height));
}

std::array<float, 16> Viewport::viewProjectionMatrix() const
{
    return m_camera.viewProjectionMatrix(static_cast<float>(m_width), static_cast<float>(m_height));
}

// ==========================================================================
//   C O O R D I N A T E   T R A N S F O R M A T I O N S
// ==========================================================================

Vector2 Viewport::screenToWorld(const Vector2& screenPos) const
{
    return m_camera.screenToWorld(screenPos, size());
}

Vector2 Viewport::worldToScreen(const Vector2& worldPos) const
{
    return m_camera.worldToScreen(worldPos, size());
}

bool Viewport::containsScreenPoint(const Vector2& screenPos) const
{
    return screenPos.x >= 0 && screenPos.x < static_cast<float>(m_width) && screenPos.y >= 0
        && screenPos.y < static_cast<float>(m_height);
}

bool Viewport::containsScreenPoint(float x, float y) const
{
    return containsScreenPoint({ x, y });
}

} // namespace aether
