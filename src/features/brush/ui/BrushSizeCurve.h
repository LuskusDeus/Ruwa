// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_WORKSPACE_BRUSHSIZECURVE_H
#define RUWA_UI_WIDGETS_WORKSPACE_BRUSHSIZECURVE_H

#include <QtGlobal>

#include <cmath>

namespace ruwa::ui::widgets {

constexpr float kBaseMaxBrushRadiusPx = 1000.0f;
constexpr float kInfiniteCanvasMaxBrushRadiusPx = 256.0f;

/// Max brush radius depends on canvas size: MaxBrush = 1500 * (1 - exp(-0.0003 * S))
/// where S = (Width + Height) / 2. Falls back to 1000 when canvas is empty.
inline float maxBrushRadiusFromCanvas(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return kBaseMaxBrushRadiusPx;
    }
    const double s = (width + height) / 2.0;
    return static_cast<float>(1500.0 * (1.0 - std::exp(-0.0003 * s)));
}

/// Base curve (0-1000 px) through control points: 0%->1, 25%->10, 50%->100, 75%->500, 100%->1000.
inline float brushRadiusBaseCurve(qreal normalizedSize)
{
    const qreal t = qBound(0.0, normalizedSize, 1.0);
    if (t <= 0.25) {
        return static_cast<float>(1.0 + 9.0 * (t / 0.25));
    }
    if (t <= 0.5) {
        return static_cast<float>(10.0 + 90.0 * ((t - 0.25) / 0.25));
    }
    if (t <= 0.75) {
        return static_cast<float>(100.0 + 400.0 * ((t - 0.5) / 0.25));
    }
    return static_cast<float>(500.0 + 500.0 * ((t - 0.75) / 0.25));
}

inline float brushRadiusFromNormalizedSizeWithMax(qreal normalizedSize, float maxBrushRadius)
{
    const float base = brushRadiusBaseCurve(normalizedSize);
    return base * (maxBrushRadius / kBaseMaxBrushRadiusPx);
}

/// Brush radius scaled by canvas: baseCurve * maxBrushFromCanvas / 1000.
inline float brushRadiusFromNormalizedSize(qreal normalizedSize, int canvasWidth, int canvasHeight)
{
    return brushRadiusFromNormalizedSizeWithMax(
        normalizedSize, maxBrushRadiusFromCanvas(canvasWidth, canvasHeight));
}

inline float maxBrushRadiusForCanvasMode(
    int canvasWidth, int canvasHeight, bool hasFiniteDocumentBounds)
{
    if (!hasFiniteDocumentBounds) {
        return kInfiniteCanvasMaxBrushRadiusPx;
    }
    return maxBrushRadiusFromCanvas(canvasWidth, canvasHeight);
}

inline float brushRadiusFromNormalizedSizeForCanvasMode(
    qreal normalizedSize, int canvasWidth, int canvasHeight, bool hasFiniteDocumentBounds)
{
    return brushRadiusFromNormalizedSizeWithMax(normalizedSize,
        maxBrushRadiusForCanvasMode(canvasWidth, canvasHeight, hasFiniteDocumentBounds));
}

inline qreal normalizedSizeFromRadiusPxWithMax(float radiusPx, float maxBrushRadius)
{
    if (radiusPx <= 0 || maxBrushRadius <= 0.0f)
        return 0.0;
    const float clampedRadius = qMin(radiusPx, maxBrushRadius);
    const float base = clampedRadius * (kBaseMaxBrushRadiusPx / maxBrushRadius);
    if (base >= 1000.0f)
        return 1.0;
    if (base <= 1.0f)
        return qMax(0.0, static_cast<qreal>(base - 1.0f) / 9.0 / 0.25);
    qreal t;
    if (base <= 10.0f) {
        t = 0.25 * (base - 1.0) / 9.0;
    } else if (base <= 100.0f) {
        t = 0.25 + 0.25 * (base - 10.0) / 90.0;
    } else if (base <= 500.0f) {
        t = 0.5 + 0.25 * (base - 100.0) / 400.0;
    } else {
        t = 0.75 + 0.25 * (base - 500.0) / 500.0;
    }
    return qBound(0.0, t, 1.0);
}

/// Inverse: normalized size (0-1) from pixel radius.
inline qreal normalizedSizeFromRadiusPx(float radiusPx, int canvasWidth, int canvasHeight)
{
    return normalizedSizeFromRadiusPxWithMax(
        radiusPx, maxBrushRadiusFromCanvas(canvasWidth, canvasHeight));
}

inline qreal normalizedSizeFromRadiusPxForCanvasMode(
    float radiusPx, int canvasWidth, int canvasHeight, bool hasFiniteDocumentBounds)
{
    return normalizedSizeFromRadiusPxWithMax(
        radiusPx, maxBrushRadiusForCanvasMode(canvasWidth, canvasHeight, hasFiniteDocumentBounds));
}

/// Preview uses radius/max so the thumbnail visually matches actual brush size.
inline qreal brushPreviewSizeNormalized(qreal normalizedSize, int canvasWidth, int canvasHeight)
{
    const float radius = brushRadiusFromNormalizedSize(normalizedSize, canvasWidth, canvasHeight);
    const float maxBrush = maxBrushRadiusFromCanvas(canvasWidth, canvasHeight);
    return static_cast<qreal>(radius) / static_cast<qreal>(maxBrush);
}

inline qreal brushPreviewSizeNormalizedForCanvasMode(
    qreal normalizedSize, int canvasWidth, int canvasHeight, bool hasFiniteDocumentBounds)
{
    const float maxBrush
        = maxBrushRadiusForCanvasMode(canvasWidth, canvasHeight, hasFiniteDocumentBounds);
    const float radius = brushRadiusFromNormalizedSizeWithMax(normalizedSize, maxBrush);
    return static_cast<qreal>(radius) / static_cast<qreal>(maxBrush);
}

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_BRUSHSIZECURVE_H
