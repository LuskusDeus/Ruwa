// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_STROKE_STROKEINPUTQUEUE_H
#define RUWA_FEATURES_CANVAS_STROKE_STROKEINPUTQUEUE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>

namespace aether {

enum class StrokeInputDevice { Stylus, Mouse };

struct StrokeInputSample {
    float worldX = 0.0f;
    float worldY = 0.0f;
    float pressure = 1.0f;
    float strokeElapsedSeconds = 0.0f;
    StrokeInputDevice inputDevice = StrokeInputDevice::Stylus;
};

namespace stroke_input_queue {

// Start shedding geometrically redundant queued samples only after the
// producer is more than roughly one frame ahead of the brush renderer. Healthy
// input streams therefore retain their exact packet sequence.
inline constexpr float kReductionActivationAgeSeconds = 0.024f;
inline constexpr float kFullReductionAgeSeconds = 0.120f;
inline constexpr float kBaseSpatialToleranceScreenPx = 0.35f;
inline constexpr float kMaxSpatialToleranceScreenPx = 1.0f;
inline constexpr float kBasePressureTolerance = 1.0f / 512.0f;
inline constexpr float kMaxPressureTolerance = 1.0f / 256.0f;
inline constexpr float kBaseTimingToleranceSeconds = 0.0015f;
inline constexpr float kMaxTimingToleranceSeconds = 0.004f;
inline constexpr float kBaseRetainedIntervalSeconds = 0.008f;
inline constexpr float kMaxRetainedIntervalSeconds = 0.016f;

struct ReductionParameters {
    float spatialToleranceScreenPx = kBaseSpatialToleranceScreenPx;
    float pressureTolerance = kBasePressureTolerance;
    float timingToleranceSeconds = kBaseTimingToleranceSeconds;
    float maxRetainedIntervalSeconds = kBaseRetainedIntervalSeconds;
};

inline float queuedAgeSeconds(const std::deque<StrokeInputSample>& samples)
{
    if (samples.size() < 2) {
        return 0.0f;
    }
    return std::max(
        0.0f, samples.back().strokeElapsedSeconds - samples.front().strokeElapsedSeconds);
}

inline ReductionParameters parametersForQueueAge(float ageSeconds)
{
    const float load = std::clamp((ageSeconds - kReductionActivationAgeSeconds)
            / (kFullReductionAgeSeconds - kReductionActivationAgeSeconds),
        0.0f, 1.0f);
    const auto interpolate = [load](float low, float high) { return low + (high - low) * load; };

    return { interpolate(kBaseSpatialToleranceScreenPx, kMaxSpatialToleranceScreenPx),
        interpolate(kBasePressureTolerance, kMaxPressureTolerance),
        interpolate(kBaseTimingToleranceSeconds, kMaxTimingToleranceSeconds),
        interpolate(kBaseRetainedIntervalSeconds, kMaxRetainedIntervalSeconds) };
}

inline bool canRemoveMiddleSample(const StrokeInputSample& first, const StrokeInputSample& middle,
    const StrokeInputSample& last, float viewportZoom, const ReductionParameters& parameters)
{
    if (first.inputDevice != middle.inputDevice || middle.inputDevice != last.inputDevice) {
        return false;
    }

    const float totalTime = last.strokeElapsedSeconds - first.strokeElapsedSeconds;
    const float middleTime = middle.strokeElapsedSeconds - first.strokeElapsedSeconds;
    if (!(totalTime > 0.0f) || !(middleTime > 0.0f) || middleTime >= totalTime
        || totalTime > parameters.maxRetainedIntervalSeconds) {
        return false;
    }

    const float chordX = last.worldX - first.worldX;
    const float chordY = last.worldY - first.worldY;
    const float chordLengthSq = chordX * chordX + chordY * chordY;
    float interpolation = middleTime / totalTime;
    float deviationWorld = 0.0f;

    if (chordLengthSq > 0.000001f) {
        const float fromFirstX = middle.worldX - first.worldX;
        const float fromFirstY = middle.worldY - first.worldY;
        interpolation = (fromFirstX * chordX + fromFirstY * chordY) / chordLengthSq;
        // A point outside the segment represents a reversal or an overshoot,
        // neither of which may be collapsed as a straight continuation.
        if (!(interpolation > 0.0f && interpolation < 1.0f)) {
            return false;
        }
        const float projectedX = first.worldX + chordX * interpolation;
        const float projectedY = first.worldY + chordY * interpolation;
        deviationWorld = std::hypot(middle.worldX - projectedX, middle.worldY - projectedY);
    } else {
        deviationWorld = std::hypot(middle.worldX - first.worldX, middle.worldY - first.worldY);
    }

    const float zoom = std::max(viewportZoom, 0.001f);
    if (deviationWorld * zoom > parameters.spatialToleranceScreenPx) {
        return false;
    }

    const float expectedPressure
        = first.pressure + (last.pressure - first.pressure) * interpolation;
    if (std::abs(middle.pressure - expectedPressure) > parameters.pressureTolerance) {
        return false;
    }

    // Preserve meaningful speed changes for Time/speed-driven brush dynamics.
    // Removing the point makes time linear along the replacement segment, so
    // only do it when that reconstructed timestamp remains close to the input.
    const float expectedTime = first.strokeElapsedSeconds + totalTime * interpolation;
    if (std::abs(middle.strokeElapsedSeconds - expectedTime) > parameters.timingToleranceSeconds) {
        return false;
    }

    return true;
}

inline std::size_t compact(
    std::deque<StrokeInputSample>& samples, float viewportZoom, float ageSeconds)
{
    if (samples.size() < 3 || ageSeconds < kReductionActivationAgeSeconds) {
        return 0;
    }

    const ReductionParameters parameters = parametersForQueueAge(ageSeconds);
    std::deque<StrokeInputSample> reduced;
    reduced.push_back(samples.front());

    for (std::size_t index = 1; index + 1 < samples.size(); ++index) {
        const StrokeInputSample& middle = samples[index];
        const StrokeInputSample& last = samples[index + 1];
        if (!canRemoveMiddleSample(reduced.back(), middle, last, viewportZoom, parameters)) {
            reduced.push_back(middle);
        }
    }
    reduced.push_back(samples.back());

    const std::size_t removed = samples.size() - reduced.size();
    samples.swap(reduced);
    return removed;
}

} // namespace stroke_input_queue
} // namespace aether

#endif // RUWA_FEATURES_CANVAS_STROKE_STROKEINPUTQUEUE_H
