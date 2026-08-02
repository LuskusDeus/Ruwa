// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_ZOOMSLIDERMAPPING_H
#define RUWA_UI_WIDGETS_ZOOMSLIDERMAPPING_H

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace ruwa::ui::widgets::zoom_slider {

inline constexpr int kMinimum = 0;
inline constexpr int kMaximum = 1000;

inline qreal valueToZoom(int value, qreal minZoom, qreal maxZoom)
{
    minZoom = std::max<qreal>(0.001, minZoom);
    maxZoom = std::max(minZoom, maxZoom);
    if (qFuzzyCompare(minZoom, maxZoom)) {
        return minZoom;
    }

    const qreal ratio = static_cast<qreal>(value - kMinimum)
        / static_cast<qreal>(kMaximum - kMinimum);
    return std::exp(std::log(minZoom) + ratio * (std::log(maxZoom) - std::log(minZoom)));
}

inline int zoomToValue(qreal zoom, qreal minZoom, qreal maxZoom)
{
    minZoom = std::max<qreal>(0.001, minZoom);
    maxZoom = std::max(minZoom, maxZoom);
    if (qFuzzyCompare(minZoom, maxZoom)) {
        return kMinimum;
    }

    const qreal clamped = qBound(minZoom, zoom, maxZoom);
    const qreal ratio
        = (std::log(clamped) - std::log(minZoom)) / (std::log(maxZoom) - std::log(minZoom));
    return kMinimum + qRound(ratio * (kMaximum - kMinimum));
}

} // namespace ruwa::ui::widgets::zoom_slider

#endif // RUWA_UI_WIDGETS_ZOOMSLIDERMAPPING_H
