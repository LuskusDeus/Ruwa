// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   C A N V A S
// ==========================================================================

#include "features/canvas/scene/Canvas.h"

namespace aether {

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

Canvas::Canvas()
{
    // Wire up dirty manager with cache and index
    m_dirtyManager.setCache(&m_compositionCache);
    m_dirtyManager.setIndex(&m_tilePositionIndex);
}

Canvas::Canvas(uint32_t width, uint32_t height)
    : m_width(width > 0 ? width : 1)
    , m_height(height > 0 ? height : 1)
{
    m_dirtyManager.setCache(&m_compositionCache);
    m_dirtyManager.setIndex(&m_tilePositionIndex);
}

// ==========================================================================
//   S I Z E
// ==========================================================================

void Canvas::setSize(uint32_t width, uint32_t height)
{
    m_width = width > 0 ? width : 1;
    m_height = height > 0 ? height : 1;
}

bool Canvas::isInfiniteCanvas() const
{
    return ruwa::core::canvas::isInfiniteCanvas(m_boundsMode);
}

bool Canvas::hasFiniteDocumentBounds() const
{
    return ruwa::core::canvas::hasFiniteDocumentBounds(m_boundsMode);
}

// ==========================================================================
//   B A C K G R O U N D
// ==========================================================================

void Canvas::setBackgroundColor(const Color& color)
{
    m_backgroundColor = color;
}

// ==========================================================================
//   B O U N D S
// ==========================================================================

Rect Canvas::bounds() const
{
    return documentBoundsRect();
}

Rect Canvas::documentBoundsRect() const
{
    return { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height) };
}

Vector2 Canvas::center() const
{
    return { static_cast<float>(m_width) / 2.0f, static_cast<float>(m_height) / 2.0f };
}

bool Canvas::contains(const Vector2& worldPoint) const
{
    if (!hasFiniteDocumentBounds()) {
        return true;
    }
    return worldPoint.x >= 0 && worldPoint.x < static_cast<float>(m_width) && worldPoint.y >= 0
        && worldPoint.y < static_cast<float>(m_height);
}

} // namespace aether
