// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   C A M E R A   2 D
// ==========================================================================

#ifndef RUWA_CORE_SCENE_CAMERA2D_H
#define RUWA_CORE_SCENE_CAMERA2D_H

#include "shared/types/Types.h"

#include <array>

namespace aether {

// ==========================================================================
//   C A M E R A   2 D
// ==========================================================================

class Camera2D {
public:
    Camera2D();

    // === Position (pan) ===
    void setPosition(const Vector2& position);
    void setPosition(float x, float y);
    void move(const Vector2& delta);
    void move(float dx, float dy);
    Vector2 position() const { return m_position; }

    // === View rotation (radians) ===
    void setRotation(float radians);
    void addRotation(float deltaRadians);
    /// Rotate by deltaRadians keeping anchorWorld pinned under anchorScreen.
    void rotateAround(
        const Vector2& anchorWorld, const Vector2& anchorScreen, const Vector2& viewportSize);
    void setRotationSmooth(float targetRadians);
    float rotation() const { return m_rotation; }

    // === Instant zoom ===
    void setZoom(float zoom);
    void zoomBy(float factor);
    void zoomAt(float factor, const Vector2& screenPoint, const Vector2& viewportSize);
    float zoom() const { return m_zoom; }

    // === Smooth zoom (interpolated) ===
    void zoomAtSmooth(float factor, const Vector2& screenPoint, const Vector2& viewportSize);
    void setZoomSmooth(float targetZoom, const Vector2& viewportSize);
    void fitToCanvasSmooth(
        const Vector2& canvasSize, const Vector2& viewportSize, float padding = 50.0f);
    void update(float dt);
    bool isAnimating() const { return m_animating; }
    bool isFitToViewAnimating() const { return m_fitToViewAnimating; }
    void stopAnimation();
    void setSmoothSpeed(float speed) { m_smoothSpeed = speed; }
    float smoothSpeed() const { return m_smoothSpeed; }

    // === Zoom limits ===
    void setZoomLimits(float minZoom, float maxZoom);
    float minZoom() const { return m_minZoom; }
    float maxZoom() const { return m_maxZoom; }

    // === Matrices (4x4 column-major) ===
    std::array<float, 16> viewMatrix() const;
    std::array<float, 16> projectionMatrix(float viewportWidth, float viewportHeight) const;
    std::array<float, 16> viewProjectionMatrix(float viewportWidth, float viewportHeight) const;

    // === Coordinate transformations ===
    Vector2 screenToWorld(const Vector2& screenPos, const Vector2& viewportSize) const;
    Vector2 worldToScreen(const Vector2& worldPos, const Vector2& viewportSize) const;

    // === Utility ===
    void reset();
    void fitToCanvas(const Vector2& canvasSize, const Vector2& viewportSize, float padding = 50.0f);
    void centerOn(const Vector2& worldPoint);
    void centerOnSmooth(const Vector2& worldPoint);

private:
    void clampZoom();
    static std::array<float, 16> multiply(
        const std::array<float, 16>& a, const std::array<float, 16>& b);

    // Helper: screenToWorld with arbitrary camera state
    Vector2 screenToWorldAt(const Vector2& screenPos, const Vector2& viewportSize, float zoom,
        const Vector2& camPos, float rotation) const;

private:
    // Current (animated) state — used for rendering
    Vector2 m_position { 0.0f, 0.0f };
    float m_zoom = 1.0f;
    float m_rotation = 0.0f;

    // Target state — where animation is heading
    Vector2 m_targetPosition { 0.0f, 0.0f };
    float m_targetZoom = 1.0f;
    float m_targetRotation = 0.0f;

    // Anchor point for zoom (keeps world point pinned under cursor)
    Vector2 m_anchorWorld { 0.0f, 0.0f };
    Vector2 m_anchorScreen { 0.0f, 0.0f };
    Vector2 m_anchorViewportSize { 1.0f, 1.0f };
    bool m_hasAnchor = false;

    // Animation
    float m_smoothSpeed = 15.0f;
    bool m_animating = false;
    bool m_fitToViewAnimating = false; // true only during fitToCanvasSmooth

    // Limits (defaults; CanvasPanel applies canvas-size–dependent limits)
    float m_minZoom = 0.8f;
    float m_maxZoom = 58.0f;
};

} // namespace aether

#endif // RUWA_CORE_SCENE_CAMERA2D_H
