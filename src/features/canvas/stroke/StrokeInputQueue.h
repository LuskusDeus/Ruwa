// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_STROKE_STROKEINPUTQUEUE_H
#define RUWA_FEATURES_CANVAS_STROKE_STROKEINPUTQUEUE_H

#include "features/brush/manager/BrushDynamicsTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <queue>
#include <vector>

// Boundary value type (plan 7.16.3 / 7.12.3): the input-device classification
// crosses the renderer-neutral painting capability, so it is defined in the
// workspace namespace; the `aether` alias keeps the legacy stroke internals
// building unchanged.

namespace ruwa::ui::workspace {

enum class StrokeInputDevice { Stylus, Mouse };

} // namespace ruwa::ui::workspace

namespace aether {

using ruwa::ui::workspace::StrokeInputDevice;

struct StrokeInputSample {
    float worldX = 0.0f;
    float worldY = 0.0f;
    float pressure = 1.0f;
    float strokeElapsedSeconds = 0.0f;
    StrokeInputDevice inputDevice = StrokeInputDevice::Stylus;
    ruwa::core::brushes::BrushInputDynamics inputDynamics {};
    /// False when the host had to manufacture ordering because the input clock
    /// repeated or moved backwards. The geometry is valid, but the artificial
    /// sub-millisecond delta must be replaced by the learned device period.
    bool timestampReliable = true;
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
// Full 0..1 pressure error is worth 24 px of positional error.
inline constexpr float kPressureSignificanceScreenPx = 24.0f;
// One second of timing error is worth 240 px of positional error.
inline constexpr float kTimingSignificanceScreenPxPerSecond = 240.0f;
inline constexpr float kTiltTolerance = 1.0f / 360.0f;
inline constexpr float kSpeedTolerance = 0.005f;
inline constexpr float kTiltSignificanceScreenPx = 180.0f;
inline constexpr float kSpeedSignificanceScreenPx = 32.0f;
inline constexpr float kTimestampReliabilitySignificanceScreenPx = 32.0f;

inline float normalizedAngleDistance(float first, float second)
{
    const float direct = std::abs(first - second);
    return std::min(direct, 1.0f - direct);
}

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
    if (first.timestampReliable != middle.timestampReliable
        || middle.timestampReliable != last.timestampReliable) {
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

    const auto& firstDynamics = first.inputDynamics;
    const auto& middleDynamics = middle.inputDynamics;
    const auto& lastDynamics = last.inputDynamics;
    if (firstDynamics.penTiltAvailable != middleDynamics.penTiltAvailable
        || middleDynamics.penTiltAvailable != lastDynamics.penTiltAvailable
        || firstDynamics.strokeSpeedAvailable != middleDynamics.strokeSpeedAvailable
        || middleDynamics.strokeSpeedAvailable != lastDynamics.strokeSpeedAvailable) {
        return false;
    }
    if (middleDynamics.penTiltAvailable) {
        const float expectedTilt = ruwa::core::brushes::interpolateNormalizedAngle(
            firstDynamics.penTilt, lastDynamics.penTilt, interpolation);
        if (normalizedAngleDistance(middleDynamics.penTilt, expectedTilt) > kTiltTolerance) {
            return false;
        }
    }
    if (middleDynamics.strokeSpeedAvailable) {
        const float expectedSpeed = firstDynamics.strokeSpeed
            + (lastDynamics.strokeSpeed - firstDynamics.strokeSpeed) * interpolation;
        if (std::abs(middleDynamics.strokeSpeed - expectedSpeed) > kSpeedTolerance) {
            return false;
        }
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

// compact() is opportunistic: it only drops samples the downstream
// distance/pressure/time interpolation can reconstruct within tolerance, so a
// noisy driver stream may legitimately shed nothing. decimateToBudget() is
// the hard guarantee the per-frame input pump needs instead: it computes how
// many samples it can afford inside the frame's time budget, and the queue
// must be brought down to that count so input latency stays bounded rather
// than piling up a backlog. Tolerances therefore only decide the ORDER in
// which points are dropped (top-down Douglas-Peucker: repeatedly split the
// span whose omitted interior point would be missed the most), never whether
// dropping is allowed.
inline std::size_t decimateToBudget(
    std::deque<StrokeInputSample>& samples, std::size_t maxSamples, float viewportZoom)
{
    if (maxSamples < 2) {
        maxSamples = 2;
    }
    if (samples.size() <= maxSamples) {
        return 0;
    }

    const float zoom = std::max(viewportZoom, 0.001f);

    // How badly interior sample k would be missed if span [i, j] collapsed to
    // its straight chord: the worst of its positional, pressure, and timing
    // deviation, all expressed in screen pixels so they can be compared and
    // the largest one drives which span gets split first. Assumes samples[i]
    // and samples[j] share an inputDevice; mixed-device spans are handled
    // separately by the caller before this is reached.
    const auto significance
        = [&samples, zoom](std::size_t i, std::size_t k, std::size_t j) -> float {
        const StrokeInputSample& first = samples[i];
        const StrokeInputSample& middle = samples[k];
        const StrokeInputSample& last = samples[j];

        const float chordX = last.worldX - first.worldX;
        const float chordY = last.worldY - first.worldY;
        const float chordLengthSq = chordX * chordX + chordY * chordY;

        // Spatial fraction of k along the chord; also doubles as the "s" used
        // to reconstruct an expected elapsed time below.
        float spatialFraction = 0.5f;
        float deviationWorld = 0.0f;
        if (chordLengthSq > 0.000001f) {
            const float fromFirstX = middle.worldX - first.worldX;
            const float fromFirstY = middle.worldY - first.worldY;
            spatialFraction = std::clamp(
                (fromFirstX * chordX + fromFirstY * chordY) / chordLengthSq, 0.0f, 1.0f);
            const float projectedX = first.worldX + chordX * spatialFraction;
            const float projectedY = first.worldY + chordY * spatialFraction;
            deviationWorld = std::hypot(middle.worldX - projectedX, middle.worldY - projectedY);
        } else {
            deviationWorld = std::hypot(middle.worldX - first.worldX, middle.worldY - first.worldY);
        }
        const float positionSignificance = deviationWorld * zoom;

        const float totalTime = last.strokeElapsedSeconds - first.strokeElapsedSeconds;
        const float timeFraction = totalTime > 0.0f
            ? std::clamp((middle.strokeElapsedSeconds - first.strokeElapsedSeconds) / totalTime,
                  0.0f, 1.0f)
            : 0.5f;
        const float expectedPressure
            = first.pressure + (last.pressure - first.pressure) * timeFraction;
        const float pressureSignificance
            = std::abs(middle.pressure - expectedPressure) * kPressureSignificanceScreenPx;

        // Reconstructed elapsed time uses the SPATIAL fraction, so a point
        // that sits mid-chord but arrived late (a slowdown) still registers,
        // which is what time/speed-driven brush dynamics need preserved.
        const float lerpedElapsed = first.strokeElapsedSeconds + totalTime * spatialFraction;
        const float timingSignificance = std::abs(middle.strokeElapsedSeconds - lerpedElapsed)
            * kTimingSignificanceScreenPxPerSecond;

        float tiltSignificance = 0.0f;
        float speedSignificance = 0.0f;
        const float timestampReliabilitySignificance
            = (first.timestampReliable != middle.timestampReliable
                  || middle.timestampReliable != last.timestampReliable)
            ? kTimestampReliabilitySignificanceScreenPx
            : 0.0f;
        const auto& firstDynamics = first.inputDynamics;
        const auto& middleDynamics = middle.inputDynamics;
        const auto& lastDynamics = last.inputDynamics;
        if (firstDynamics.penTiltAvailable != middleDynamics.penTiltAvailable
            || middleDynamics.penTiltAvailable != lastDynamics.penTiltAvailable) {
            tiltSignificance = kTiltSignificanceScreenPx;
        } else if (middleDynamics.penTiltAvailable) {
            const float expectedTilt = ruwa::core::brushes::interpolateNormalizedAngle(
                firstDynamics.penTilt, lastDynamics.penTilt, spatialFraction);
            tiltSignificance = normalizedAngleDistance(middleDynamics.penTilt, expectedTilt)
                * kTiltSignificanceScreenPx;
        }
        if (firstDynamics.strokeSpeedAvailable != middleDynamics.strokeSpeedAvailable
            || middleDynamics.strokeSpeedAvailable != lastDynamics.strokeSpeedAvailable) {
            speedSignificance = kSpeedSignificanceScreenPx;
        } else if (middleDynamics.strokeSpeedAvailable) {
            const float expectedSpeed = firstDynamics.strokeSpeed
                + (lastDynamics.strokeSpeed - firstDynamics.strokeSpeed) * spatialFraction;
            speedSignificance
                = std::abs(middleDynamics.strokeSpeed - expectedSpeed) * kSpeedSignificanceScreenPx;
        }

        float result = std::max({ positionSignificance, pressureSignificance, timingSignificance,
            tiltSignificance, speedSignificance, timestampReliabilitySignificance });
        if (!std::isfinite(result)) {
            result = 0.0f;
        }
        return result;
    };

    struct Span {
        float significance;
        std::size_t begin;
        std::size_t end;
        std::size_t split;
    };
    struct SpanLess {
        bool operator()(const Span& a, const Span& b) const
        {
            return a.significance < b.significance;
        }
    };

    // A span whose endpoints belong to different input devices can never be
    // collapsed into one interpolated segment (canRemoveMiddleSample refuses
    // this outright), so its interior is treated as maximally significant and
    // bisected to converge on the device boundary before any ordinary,
    // distance-driven split is considered.
    const auto findSplit = [&samples, &significance](std::size_t begin, std::size_t end) -> Span {
        if (samples[begin].inputDevice != samples[end].inputDevice) {
            // Larger than any realistic screen-pixel deviation, so this span
            // always outranks ordinary distance/pressure/timing splits.
            constexpr float kMixedDeviceSentinelSignificance = 1.0e30f;
            const std::size_t split = begin + (end - begin) / 2;
            return { kMixedDeviceSentinelSignificance, begin, end, split };
        }
        float bestSignificance = 0.0f;
        std::size_t bestSplit = begin + 1;
        for (std::size_t k = begin + 1; k < end; ++k) {
            const float candidate = significance(begin, k, end);
            if (candidate > bestSignificance) {
                bestSignificance = candidate;
                bestSplit = k;
            }
        }
        return { bestSignificance, begin, end, bestSplit };
    };

    std::priority_queue<Span, std::vector<Span>, SpanLess> queue;
    const std::size_t lastIndex = samples.size() - 1;
    queue.push(findSplit(0, lastIndex));

    std::vector<char> kept(samples.size(), 0);
    kept.front() = 1;
    kept.back() = 1;
    std::size_t keptCount = 2;

    while (keptCount < maxSamples && !queue.empty()) {
        const Span span = queue.top();
        queue.pop();
        if (!(span.significance > 0.0f)) {
            break;
        }
        kept[span.split] = 1;
        ++keptCount;
        if (span.split > span.begin + 1) {
            queue.push(findSplit(span.begin, span.split));
        }
        if (span.end > span.split + 1) {
            queue.push(findSplit(span.split, span.end));
        }
    }

    std::deque<StrokeInputSample> reduced;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (kept[index]) {
            reduced.push_back(samples[index]);
        }
    }

    const std::size_t removed = samples.size() - reduced.size();
    samples.swap(reduced);
    return removed;
}

} // namespace stroke_input_queue
} // namespace aether

#endif // RUWA_FEATURES_CANVAS_STROKE_STROKEINPUTQUEUE_H
