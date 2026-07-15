// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_RENDERING_OVERLAYS_OVERLAYVISUALBASE_H
#define RUWA_CORE_RENDERING_OVERLAYS_OVERLAYVISUALBASE_H

#include <algorithm>
#include <chrono>
#include <cmath>

namespace aether {

class OverlayVisualBase {
public:
    struct ElementMetrics {
        float framePaddingPx = 10.0f;
        float realBoundsInsetPx = 2.0f;
        float realBoundsStrokePx = 1.0f;
        float handleStrokePx = 2.0f;
        float cornerLengthPx = 24.0f;
        float sideLengthPx = 60.0f;
        float edgeGapPx = 8.0f;
    };

    virtual ~OverlayVisualBase() = default;

    bool isAnimating() const { return m_animating; }
    bool isVisible() const { return m_visible; }
    float animationProgress() const { return m_animProgress; }
    void setElementMetrics(const ElementMetrics& metrics) { m_metrics = metrics; }
    const ElementMetrics& elementMetrics() const { return m_metrics; }

protected:
    void setVisibleImmediate(bool visible, float progress = 1.0f)
    {
        m_visible = visible;
        m_animating = false;
        m_animProgress = std::clamp(progress, 0.0f, 1.0f);
        m_animStartProgress = m_animProgress;
        m_animTargetProgress = m_animProgress;
    }

    void startVisibilityAnimation(float target)
    {
        m_animStartProgress = m_animProgress;
        m_animTargetProgress = std::clamp(target, 0.0f, 1.0f);
        m_animStart = std::chrono::steady_clock::now();
        m_animating = true;
        if (m_animTargetProgress > 0.0f) {
            m_visible = true;
        }
    }

    void updateVisibilityAnimation(float durationMs = 120.0f)
    {
        if (!m_animating)
            return;

        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float, std::milli>(now - m_animStart).count();
        const float t = std::clamp(elapsed / durationMs, 0.0f, 1.0f);
        const float eased = 1.0f - std::pow(1.0f - t, 3.0f); // OutCubic
        m_animProgress = m_animStartProgress + (m_animTargetProgress - m_animStartProgress) * eased;

        if (t >= 1.0f) {
            m_animProgress = m_animTargetProgress;
            m_animating = false;
            if (m_animProgress <= 0.0001f) {
                m_visible = false;
            }
        }
    }

private:
    ElementMetrics m_metrics;
    std::chrono::steady_clock::time_point m_animStart;
    float m_animStartProgress = 0.0f;
    float m_animTargetProgress = 0.0f;
    float m_animProgress = 0.0f;
    bool m_animating = false;
    bool m_visible = false;
};

} // namespace aether

#endif // RUWA_CORE_RENDERING_OVERLAYS_OVERLAYVISUALBASE_H
