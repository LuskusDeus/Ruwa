// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   C A M E R A   2 D
// ==========================================================================

#include "features/canvas/scene/Camera2D.h"

#include <algorithm>
#include <cmath>

namespace aether {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

float normalizeRadians0ToTwoPi(float radians)
{
    radians = std::fmod(radians, kTwoPi);
    if (radians < 0.0f)
        radians += kTwoPi;
    return radians;
}

float shortestSignedAngleDelta(float from, float to)
{
    float delta = to - from;
    if (delta > kPi)
        delta -= kTwoPi;
    if (delta < -kPi)
        delta += kTwoPi;
    return delta;
}

float angularDistance(float a, float b)
{
    return std::abs(shortestSignedAngleDelta(a, b));
}

Vector2 screenOffsetToWorldOffset(const Vector2& screenOffset, float zoom, float rotation)
{
    const float invZoom = 1.0f / zoom;
    const float dx = screenOffset.x * invZoom;
    const float dy = screenOffset.y * invZoom;
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);

    // Inverse camera rotation: screen-space offset -> world-space offset.
    return { dx * c + dy * s, -dx * s + dy * c };
}

} // namespace

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

Camera2D::Camera2D() = default;

// ==========================================================================
//   P O S I T I O N
// ==========================================================================

void Camera2D::setPosition(const Vector2& position)
{
    m_position = position;
    m_targetPosition = position;
}

void Camera2D::setPosition(float x, float y)
{
    setPosition({ x, y });
}

void Camera2D::move(const Vector2& delta)
{
    if (m_fitToViewAnimating) {
        stopAnimation();
    }
    m_position += delta;
    m_targetPosition += delta;

    // Shift the anchor so smooth zoom doesn't fight with panning
    if (m_hasAnchor) {
        m_anchorWorld += delta;
    }
}

void Camera2D::move(float dx, float dy)
{
    move({ dx, dy });
}

// ==========================================================================
//   R O T A T I O N
// ==========================================================================

void Camera2D::setRotation(float radians)
{
    const float normalized = normalizeRadians0ToTwoPi(radians);
    m_rotation = normalized;
    m_targetRotation = normalized;
}

void Camera2D::addRotation(float deltaRadians)
{
    m_rotation = normalizeRadians0ToTwoPi(m_rotation + deltaRadians);
    m_targetRotation = m_rotation;
}

void Camera2D::rotateAround(
    const Vector2& anchorWorld, const Vector2& anchorScreen, const Vector2& viewportSize)
{
    const Vector2 screenOffset = anchorScreen - viewportSize * 0.5f;
    const Vector2 worldOffset = screenOffsetToWorldOffset(screenOffset, m_zoom, m_rotation);
    m_position = anchorWorld - worldOffset;
    m_targetPosition = m_position;
}

void Camera2D::setRotationSmooth(float targetRadians)
{
    const float normalizedTarget = normalizeRadians0ToTwoPi(targetRadians);
    if (angularDistance(m_rotation, normalizedTarget) < 0.0001f)
        return;
    m_targetRotation = normalizedTarget;
    m_hasAnchor = false;
    m_animating = true;
}

// ==========================================================================
//   I N S T A N T   Z O O M
// ==========================================================================

void Camera2D::setZoom(float zoom)
{
    m_zoom = zoom;
    m_targetZoom = zoom;
    m_animating = false;
    clampZoom();
}

void Camera2D::zoomBy(float factor)
{
    m_zoom *= factor;
    m_targetZoom = m_zoom;
    m_animating = false;
    clampZoom();
}

void Camera2D::zoomAt(float factor, const Vector2& screenPoint, const Vector2& viewportSize)
{
    Vector2 worldBefore = screenToWorld(screenPoint, viewportSize);

    m_zoom *= factor;
    clampZoom();

    Vector2 worldAfter = screenToWorld(screenPoint, viewportSize);
    m_position += worldBefore - worldAfter;

    m_targetZoom = m_zoom;
    m_targetPosition = m_position;
    m_animating = false;
}

void Camera2D::setZoomLimits(float minZoom, float maxZoom)
{
    m_minZoom = std::max(0.001f, minZoom);
    m_maxZoom = std::max(m_minZoom, maxZoom);
    clampZoom();
}

void Camera2D::clampZoom()
{
    m_zoom = std::clamp(m_zoom, m_minZoom, m_maxZoom);
    m_targetZoom = std::clamp(m_targetZoom, m_minZoom, m_maxZoom);
}

// ==========================================================================
//   S M O O T H   Z O O M
// ==========================================================================

void Camera2D::zoomAtSmooth(float factor, const Vector2& screenPoint, const Vector2& viewportSize)
{
    float newTargetZoom = m_targetZoom * factor;
    newTargetZoom = std::clamp(newTargetZoom, m_minZoom, m_maxZoom);

    // The world point under cursor — this is what must stay pinned
    // Compute it from the TARGET state (so repeated scroll events accumulate correctly)
    m_anchorWorld = screenToWorldAt(
        screenPoint, viewportSize, m_targetZoom, m_targetPosition, m_targetRotation);
    m_anchorScreen = screenPoint;
    m_anchorViewportSize = viewportSize;

    m_targetZoom = newTargetZoom;
    m_animating = true;
    m_fitToViewAnimating = false; // Regular zoom, not fit-to-view
    m_hasAnchor = true;
}

void Camera2D::setZoomSmooth(float targetZoom, const Vector2& viewportSize)
{
    targetZoom = std::clamp(targetZoom, m_minZoom, m_maxZoom);
    if (std::abs(m_zoom - targetZoom) < 0.0001f)
        return;

    Vector2 centerScreen(viewportSize.x * 0.5f, viewportSize.y * 0.5f);
    m_anchorWorld = screenToWorldAt(centerScreen, viewportSize, m_zoom, m_position, m_rotation);
    m_anchorScreen = centerScreen;
    m_anchorViewportSize = viewportSize;

    m_targetZoom = targetZoom;
    m_animating = true;
    m_fitToViewAnimating = false; // Regular zoom, not fit-to-view
    m_hasAnchor = true;
}

void Camera2D::fitToCanvasSmooth(
    const Vector2& canvasSize, const Vector2& viewportSize, float padding)
{
    float availableWidth = viewportSize.x - padding * 2.0f;
    float availableHeight = viewportSize.y - padding * 2.0f;

    if (availableWidth <= 0 || availableHeight <= 0) {
        setZoom(1.0f);
        m_fitToViewAnimating = false;
        return;
    }

    float zoomX = availableWidth / canvasSize.x;
    float zoomY = availableHeight / canvasSize.y;
    float targetZoom = std::min(zoomX, zoomY);
    targetZoom = std::clamp(targetZoom, m_minZoom, m_maxZoom);

    Vector2 targetPosition(canvasSize.x / 2.0f, canvasSize.y / 2.0f);

    m_targetZoom = targetZoom;
    m_targetPosition = targetPosition;
    m_targetRotation = 0.0f;
    m_hasAnchor = false;
    m_animating = true;
    m_fitToViewAnimating = true;
}

void Camera2D::update(float dt)
{
    if (!m_animating)
        return;

    const float t = 1.0f - std::exp(-m_smoothSpeed * dt);

    // Interpolate zoom in logarithmic space for perceptually uniform speed
    float logCurrent = std::log(m_zoom);
    float logTarget = std::log(m_targetZoom);
    float logNew = logCurrent + (logTarget - logCurrent) * t;
    m_zoom = std::exp(logNew);

    // Interpolate rotation along the shortest angular path.
    m_rotation = normalizeRadians0ToTwoPi(
        m_rotation + shortestSignedAngleDelta(m_rotation, m_targetRotation) * t);

    // Derive position from zoom to keep anchor point pinned under cursor
    if (m_hasAnchor) {
        Vector2 screenOffset = m_anchorScreen - m_anchorViewportSize * 0.5f;
        const Vector2 worldOffset = screenOffsetToWorldOffset(screenOffset, m_zoom, m_rotation);
        m_position = m_anchorWorld - worldOffset;

        // Also update target position to match (so panning after zoom works)
        const Vector2 targetWorldOffset
            = screenOffsetToWorldOffset(screenOffset, m_targetZoom, m_targetRotation);
        m_targetPosition = m_anchorWorld - targetWorldOffset;
    } else {
        // Interpolate position (e.g. for fitToCanvasSmooth)
        m_position.x += (m_targetPosition.x - m_position.x) * t;
        m_position.y += (m_targetPosition.y - m_position.y) * t;
    }

    // Snap when close enough
    float logDiff = std::abs(logNew - logTarget);
    float posDiff
        = std::abs(m_position.x - m_targetPosition.x) + std::abs(m_position.y - m_targetPosition.y);
    float rotDiff = angularDistance(m_rotation, m_targetRotation);
    if (logDiff < 0.0005f && posDiff < 0.01f && rotDiff < 0.0005f) {
        m_zoom = m_targetZoom;
        m_position = m_targetPosition;
        m_rotation = normalizeRadians0ToTwoPi(m_targetRotation);
        m_targetRotation = m_rotation;
        m_animating = false;
        m_fitToViewAnimating = false;
        m_hasAnchor = false;
    }

    clampZoom();
}

void Camera2D::stopAnimation()
{
    m_targetZoom = m_zoom;
    m_targetPosition = m_position;
    m_targetRotation = m_rotation;
    m_animating = false;
    m_fitToViewAnimating = false;
}

// ==========================================================================
//   M A T R I C E S
// ==========================================================================

std::array<float, 16> Camera2D::viewMatrix() const
{
    const float c = std::cos(m_rotation);
    const float s = std::sin(m_rotation);
    const float z = m_zoom;
    const float m00 = z * c;
    const float m01 = z * s;
    const float m10 = -z * s;
    const float m11 = z * c;
    const float tx = -m_position.x * m00 - m_position.y * m10;
    const float ty = -m_position.x * m01 - m_position.y * m11;

    return { m00, m01, 0.0f, 0.0f, m10, m11, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, tx, ty, 0.0f,
        1.0f };
}

std::array<float, 16> Camera2D::projectionMatrix(float viewportWidth, float viewportHeight) const
{
    // Orthographic projection: viewport coords -> NDC
    // Y flipped for screen-space convention (Y-down)
    float sx = 1.0f / (viewportWidth / 2.0f);
    float sy = -1.0f / (viewportHeight / 2.0f);

    return { sx, 0.0f, 0.0f, 0.0f, 0.0f, sy, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.0f };
}

std::array<float, 16> Camera2D::viewProjectionMatrix(
    float viewportWidth, float viewportHeight) const
{
    return multiply(projectionMatrix(viewportWidth, viewportHeight), viewMatrix());
}

std::array<float, 16> Camera2D::multiply(
    const std::array<float, 16>& a, const std::array<float, 16>& b)
{
    std::array<float, 16> result {};

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            result[col * 4 + row] = sum;
        }
    }

    return result;
}

// ==========================================================================
//   C O O R D I N A T E   T R A N S F O R M A T I O N S
// ==========================================================================

Vector2 Camera2D::screenToWorld(const Vector2& screenPos, const Vector2& viewportSize) const
{
    return screenToWorldAt(screenPos, viewportSize, m_zoom, m_position, m_rotation);
}

Vector2 Camera2D::worldToScreen(const Vector2& worldPos, const Vector2& viewportSize) const
{
    const float cx = viewportSize.x / 2.0f;
    const float cy = viewportSize.y / 2.0f;
    const float dx = worldPos.x - m_position.x;
    const float dy = worldPos.y - m_position.y;
    const float c = std::cos(m_rotation);
    const float s = std::sin(m_rotation);
    const float screenX = (dx * c - dy * s) * m_zoom + cx;
    const float screenY = (dx * s + dy * c) * m_zoom + cy;
    return { screenX, screenY };
}

Vector2 Camera2D::screenToWorldAt(const Vector2& screenPos, const Vector2& viewportSize, float zoom,
    const Vector2& camPos, float rotation) const
{
    const Vector2 screenOffset = screenPos - viewportSize * 0.5f;
    return camPos + screenOffsetToWorldOffset(screenOffset, zoom, rotation);
}

// ==========================================================================
//   U T I L I T Y
// ==========================================================================

void Camera2D::reset()
{
    m_position = { 0.0f, 0.0f };
    m_targetPosition = { 0.0f, 0.0f };
    m_zoom = 1.0f;
    m_targetZoom = 1.0f;
    m_rotation = 0.0f;
    m_targetRotation = 0.0f;
    m_animating = false;
}

void Camera2D::fitToCanvas(const Vector2& canvasSize, const Vector2& viewportSize, float padding)
{
    m_position = { canvasSize.x / 2.0f, canvasSize.y / 2.0f };
    m_targetPosition = m_position;
    m_rotation = 0.0f;
    m_targetRotation = 0.0f;

    float availableWidth = viewportSize.x - padding * 2.0f;
    float availableHeight = viewportSize.y - padding * 2.0f;

    if (availableWidth <= 0 || availableHeight <= 0) {
        m_zoom = 1.0f;
        m_targetZoom = 1.0f;
        return;
    }

    float zoomX = availableWidth / canvasSize.x;
    float zoomY = availableHeight / canvasSize.y;

    m_zoom = std::min(zoomX, zoomY);
    m_targetZoom = m_zoom;
    m_animating = false;
    clampZoom();
}

void Camera2D::centerOn(const Vector2& worldPoint)
{
    m_position = worldPoint;
    m_targetPosition = worldPoint;
}

void Camera2D::centerOnSmooth(const Vector2& worldPoint)
{
    m_targetPosition = worldPoint;
    m_hasAnchor = false;
    m_animating = true;
}

} // namespace aether
