// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_TILES_STROKETAPERSTATE_H
#define RUWA_CORE_TILES_STROKETAPERSTATE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace aether::stroke_taper {

inline constexpr float kEpsilon = 0.0001f;
inline constexpr std::size_t kMaxAffectedDabs = 1500;

struct State {
    std::size_t dabCount = 0;
    std::size_t startDabCount = 0;
    std::size_t endDabCount = 0;
    bool applicable = false;

    bool hasEffect() const { return applicable && (startDabCount > 0 || endDabCount > 0); }

    bool touchesWholeStroke() const
    {
        if (!hasEffect() || dabCount == 0) {
            return false;
        }
        return startDabCount >= dabCount || endDabCount >= dabCount
            || (startDabCount > 0 && endDabCount > 0 && (startDabCount + endDabCount) >= dabCount);
    }

    std::size_t endRangeStart() const
    {
        if (endDabCount == 0) {
            return dabCount;
        }
        return endDabCount >= dabCount ? 0 : dabCount - endDabCount;
    }
};

inline std::size_t affectedDabCount(float taper, std::size_t dabCount)
{
    if (dabCount == 0) {
        return 0;
    }
    const float clampedTaper = std::clamp(taper, 0.0f, 1.0f);
    if (clampedTaper <= kEpsilon) {
        return 0;
    }
    const auto affected
        = static_cast<std::size_t>(std::ceil(clampedTaper * static_cast<float>(kMaxAffectedDabs)));
    return std::min(dabCount, std::max<std::size_t>(1, affected));
}

inline State makeState(std::size_t dabCount, float startTaper, float endTaper, bool applicable)
{
    State state;
    state.dabCount = dabCount;
    state.applicable = applicable;
    if (applicable) {
        state.startDabCount = affectedDabCount(startTaper, dabCount);
        state.endDabCount = affectedDabCount(endTaper, dabCount);
    }
    return state;
}

inline bool requiresReplay(bool applicable, bool headCaptured, float capturedStartTaper,
    bool startMayProduceTaper, bool endMayProduceTaper)
{
    if (!applicable) {
        return false;
    }
    const bool startNeedsReplay
        = headCaptured ? capturedStartTaper > kEpsilon : startMayProduceTaper;
    return startNeedsReplay || endMayProduceTaper;
}

inline float scaleForEdgeIndex(std::size_t edgeIndex, std::size_t affectedCount)
{
    if (affectedCount <= 1) {
        return 0.0f;
    }
    return std::clamp(
        static_cast<float>(edgeIndex) / static_cast<float>(affectedCount - 1), 0.0f, 1.0f);
}

inline float scaleForDab(const State& state, std::size_t dabIndex)
{
    if (!state.applicable || state.dabCount == 0 || dabIndex >= state.dabCount) {
        return 1.0f;
    }

    float scale = 1.0f;
    if (state.startDabCount > 0 && dabIndex < state.startDabCount) {
        scale = std::min(scale, scaleForEdgeIndex(dabIndex, state.startDabCount));
    }
    const std::size_t distanceToEnd = state.dabCount - 1 - dabIndex;
    if (state.endDabCount > 0 && distanceToEnd < state.endDabCount) {
        scale = std::min(scale, scaleForEdgeIndex(distanceToEnd, state.endDabCount));
    }
    return scale;
}

inline std::size_t updateRangeStart(
    const State& state, std::size_t previousPreviewDabCount, std::size_t previousTailStart)
{
    std::size_t updateStart = std::min(previousPreviewDabCount, state.dabCount);
    if (previousTailStart != std::numeric_limits<std::size_t>::max()) {
        updateStart = std::min(updateStart, previousTailStart);
    }
    return std::min(updateStart, state.endRangeStart());
}

} // namespace aether::stroke_taper

#endif // RUWA_CORE_TILES_STROKETAPERSTATE_H
