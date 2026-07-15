// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_STROKESTABILIZER_H
#define RUWA_CORE_BRUSHES_STROKESTABILIZER_H

#include <algorithm>
#include <cmath>

namespace ruwa::core::brushes {

// SAI / Clip Studio Paint style stabilizer.
//
// Implemented as a two-stage cascaded EWMA. Each call subdivides the
// elapsed wall time into ~0.5 ms substeps with target linearly
// interpolated between the previous raw input and the current one, so
// the integrated trajectory of the filter state is smooth and
// independent of how bursty the call rate is. Without this, mouse
// events arriving in tight clusters separated by 5-8 ms gaps produced
// wildly uneven per-call stab advances (small in clusters, big in
// gaps), which the consumer's Bezier smoother turned into visible
// polygon kinks at low stabilization.
//
// End-of-stroke catch-up is automatic: when the host keeps re-feeding
// the last cursor position on a timer, the filter converges
// exponentially to the target.

// Steady-state lag at stabilization = 1.0.
inline constexpr float kStabilizerMaxLagMs = 1000.0f;
// Substep size for in-call integration. 0.5 ms keeps integration error
// negligible for the smallest sensible lagMs while bounding work per
// call (a generous 20 ms call needs only 40 substeps of trivial math).
inline constexpr double kStabilizerSubstepMs = 0.5;
inline constexpr double kStabilizerOutputStepMs = 1.0;
// Hard cap on substeps per call to keep a long pause/resume cheap. At
// 0.5 ms substeps this covers 100 ms of catch-up; longer than that the
// filter has converged anyway.
inline constexpr int kStabilizerMaxSubsteps = 200;

struct StrokeStabilizerPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct StrokeStabilizerState {
    bool valid = false;
    StrokeStabilizerPoint y1 {}; // first EWMA stage
    StrokeStabilizerPoint y2 {}; // second EWMA stage = output
    StrokeStabilizerPoint output {};
    StrokeStabilizerPoint target {};
    // Previous call's raw target. Used as the start of the linearly
    // interpolated segment that this call's substeps integrate over.
    float lastTargetX = 0.0f;
    float lastTargetY = 0.0f;
    double lastTimeMs = 0.0;
};

// `stabilization` (0..1) → target steady-state lag in ms.
//
// Quadratic curve keeps the lower half of the slider subtle:
//   0.15 →  ~22 ms  (kills jitter, barely visible)
//   0.30 →  ~90 ms
//   0.50 → ~250 ms  (clear lasso pull)
//   0.75 → ~562 ms
//   1.00 → 1000 ms  (very strong)
inline float stabilizationTauMs(float stabilization)
{
    if (!std::isfinite(stabilization)) {
        return 0.0f;
    }
    const float s = std::clamp(stabilization, 0.0f, 1.0f);
    return s * s * kStabilizerMaxLagMs;
}

namespace detail {

// Continuous-time per-stage smoothing factor for elapsed dtMs.
// Each stage of the 2-stage cascade has time constant tauMs = lagMs/2.
// α = 1 − exp(−dtMs / tauMs).
inline float stabilizerAlpha(float lagMs, double dtMs)
{
    if (!std::isfinite(lagMs) || lagMs <= 0.0f || !(dtMs > 0.0)) {
        return 1.0f;
    }
    const double tauMs = static_cast<double>(lagMs) * 0.5;
    return static_cast<float>(1.0 - std::exp(-dtMs / tauMs));
}

inline void ewmaStep(StrokeStabilizerState& s, float ix, float iy, float alpha)
{
    s.y1.x += alpha * (ix - s.y1.x);
    s.y1.y += alpha * (iy - s.y1.y);
    s.y2.x += alpha * (s.y1.x - s.y2.x);
    s.y2.y += alpha * (s.y1.y - s.y2.y);
}

} // namespace detail

template <typename EmitPoint>
inline StrokeStabilizerPoint sampleStrokeStabilizerPath(StrokeStabilizerState& state, float targetX,
    float targetY, float lagMs, double nowMs, bool reset, EmitPoint emitPoint)
{
    if (!std::isfinite(nowMs)) {
        nowMs = state.lastTimeMs;
    }

    if (reset || !state.valid || !(lagMs > 0.0f)) {
        state.valid = true;
        state.y1 = { targetX, targetY };
        state.y2 = { targetX, targetY };
        state.target = { targetX, targetY };
        state.output = { targetX, targetY };
        state.lastTargetX = targetX;
        state.lastTargetY = targetY;
        state.lastTimeMs = nowMs;
        emitPoint(state.output, nowMs);
        return state.output;
    }

    if (nowMs <= state.lastTimeMs) {
        return state.output;
    }
    const double startTimeMs = state.lastTimeMs;
    const double dtMs = nowMs - startTimeMs;
    state.lastTimeMs = nowMs;
    state.target = { targetX, targetY };

    if (dtMs > 0.0) {
        // Subdivide so the in-call integration sees a target that
        // moves linearly from lastTarget to (targetX, targetY) instead
        // of jumping. This makes per-call output advance proportional
        // to actual cursor distance, not to call dt — which is what
        // kept the consumer's Bezier smoother seeing wildly uneven
        // step sizes when raw events arrived in bursts.
        const int substeps = std::clamp(
            static_cast<int>(std::ceil(dtMs / kStabilizerSubstepMs)), 1, kStabilizerMaxSubsteps);
        const double subDt = dtMs / static_cast<double>(substeps);
        const float alpha = detail::stabilizerAlpha(lagMs, subDt);
        const float startX = state.lastTargetX;
        const float startY = state.lastTargetY;
        const float dx = targetX - startX;
        const float dy = targetY - startY;
        const float invN = 1.0f / static_cast<float>(substeps);
        double nextEmitMs = startTimeMs + kStabilizerOutputStepMs;
        for (int i = 1; i <= substeps; ++i) {
            const float frac = static_cast<float>(i) * invN;
            const float ix = startX + dx * frac;
            const float iy = startY + dy * frac;
            detail::ewmaStep(state, ix, iy, alpha);
            const double sampleTimeMs = startTimeMs + subDt * static_cast<double>(i);
            if (sampleTimeMs + 0.0001 >= nextEmitMs || i == substeps) {
                state.output = state.y2;
                emitPoint(state.output, sampleTimeMs);
                while (nextEmitMs <= sampleTimeMs + 0.0001) {
                    nextEmitMs += kStabilizerOutputStepMs;
                }
            }
        }
    }

    state.lastTargetX = targetX;
    state.lastTargetY = targetY;
    state.output = state.y2;

    // Snap once essentially converged so the catch-up timer can stop.
    const float dx = state.target.x - state.output.x;
    const float dy = state.target.y - state.output.y;
    if (dx * dx + dy * dy <= 0.01f) {
        state.output = state.target;
        state.y1 = state.output;
        state.y2 = state.output;
    }
    return state.output;
}

inline StrokeStabilizerPoint sampleStrokeStabilizer(StrokeStabilizerState& state, float targetX,
    float targetY, float lagMs, double nowMs, bool reset = false)
{
    return sampleStrokeStabilizerPath(
        state, targetX, targetY, lagMs, nowMs, reset, [](const StrokeStabilizerPoint&, double) { });
}

inline bool hasPendingStrokeStabilizer(
    const StrokeStabilizerState& state, float targetX, float targetY, float epsilonSq = 0.25f)
{
    if (!state.valid) {
        return false;
    }
    const float dx = targetX - state.output.x;
    const float dy = targetY - state.output.y;
    return (dx * dx + dy * dy) > epsilonSq;
}

inline void translateStrokeStabilizer(StrokeStabilizerState& state, float dx, float dy)
{
    if (!state.valid || (std::abs(dx) <= 0.0001f && std::abs(dy) <= 0.0001f)) {
        return;
    }
    state.y1.x += dx;
    state.y1.y += dy;
    state.y2.x += dx;
    state.y2.y += dy;
    state.output.x += dx;
    state.output.y += dy;
    state.target.x += dx;
    state.target.y += dy;
    state.lastTargetX += dx;
    state.lastTargetY += dy;
}

inline void clearStrokeStabilizer(StrokeStabilizerState& state)
{
    state = {};
}

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_STROKESTABILIZER_H
