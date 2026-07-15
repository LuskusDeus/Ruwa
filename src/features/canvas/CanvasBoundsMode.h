// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_CANVAS_BOUNDSMODE_H
#define RUWA_CORE_CANVAS_BOUNDSMODE_H

namespace ruwa::core::canvas {

enum class CanvasBoundsMode { Bounded = 0, Infinite = 1 };

inline bool isInfiniteCanvas(CanvasBoundsMode mode)
{
    return mode == CanvasBoundsMode::Infinite;
}

inline bool hasFiniteDocumentBounds(CanvasBoundsMode mode)
{
    return mode == CanvasBoundsMode::Bounded;
}

} // namespace ruwa::core::canvas

#endif // RUWA_CORE_CANVAS_BOUNDSMODE_H
