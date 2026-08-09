// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/stroke/StrokeInputQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <deque>

namespace {

aether::StrokeInputSample sample(float x, float y, float pressure, float elapsedSeconds)
{
    return { x, y, pressure, elapsedSeconds, aether::StrokeInputDevice::Stylus };
}

} // namespace

TEST_CASE("stroke input queue leaves a healthy stream untouched", "[brush][input]")
{
    std::deque<aether::StrokeInputSample> samples;
    for (int index = 0; index < 20; ++index) {
        samples.push_back(
            sample(static_cast<float>(index), 0.0f, 0.5f, static_cast<float>(index) * 0.001f));
    }

    const std::size_t removed = aether::stroke_input_queue::compact(samples, 1.0f, 0.019f);

    CHECK(removed == 0);
    CHECK(samples.size() == 20);
}

TEST_CASE("stroke input queue compacts a high-rate straight path", "[brush][input]")
{
    std::deque<aether::StrokeInputSample> samples;
    for (int index = 0; index <= 200; ++index) {
        const float t = static_cast<float>(index) * 0.001f;
        samples.push_back(sample(static_cast<float>(index) * 0.5f, 10.0f, 0.25f + t, t));
    }
    const aether::StrokeInputSample first = samples.front();
    const aether::StrokeInputSample last = samples.back();

    const std::size_t removed = aether::stroke_input_queue::compact(
        samples, 1.0f, aether::stroke_input_queue::queuedAgeSeconds(samples));

    REQUIRE(removed > 150);
    CHECK(samples.front().worldX == first.worldX);
    CHECK(samples.front().strokeElapsedSeconds == first.strokeElapsedSeconds);
    CHECK(samples.back().worldX == last.worldX);
    CHECK(samples.back().pressure == last.pressure);
    CHECK(samples.back().strokeElapsedSeconds == last.strokeElapsedSeconds);
}

TEST_CASE("stroke input queue remains small under continuous high-rate input", "[brush][input]")
{
    std::deque<aether::StrokeInputSample> samples;
    std::size_t samplesSinceCompaction = 0;
    std::size_t removed = 0;
    for (int index = 0; index <= 1000; ++index) {
        const float t = static_cast<float>(index) * 0.001f;
        samples.push_back(sample(static_cast<float>(index) * 0.25f, 4.0f, 0.5f, t));
        ++samplesSinceCompaction;

        const float queuedAge = aether::stroke_input_queue::queuedAgeSeconds(samples);
        if (queuedAge >= aether::stroke_input_queue::kReductionActivationAgeSeconds
            && samplesSinceCompaction >= 8) {
            removed += aether::stroke_input_queue::compact(samples, 1.0f, queuedAge);
            samplesSinceCompaction = 0;
        }
    }

    CHECK(removed > 900);
    CHECK(samples.size() < 100);
    CHECK(samples.front().strokeElapsedSeconds == 0.0f);
    CHECK(std::abs(samples.back().strokeElapsedSeconds - 1.0f) < 0.000001f);
}

TEST_CASE("stroke input queue preserves a sharp corner", "[brush][input]")
{
    std::deque<aether::StrokeInputSample> samples;
    for (int index = 0; index <= 50; ++index) {
        samples.push_back(
            sample(static_cast<float>(index), 0.0f, 0.5f, static_cast<float>(index) * 0.001f));
    }
    for (int index = 1; index <= 50; ++index) {
        samples.push_back(sample(
            50.0f, static_cast<float>(index), 0.5f, static_cast<float>(50 + index) * 0.001f));
    }

    aether::stroke_input_queue::compact(
        samples, 1.0f, aether::stroke_input_queue::queuedAgeSeconds(samples));

    const auto corner = std::find_if(samples.begin(), samples.end(),
        [](const auto& point) { return point.worldX == 50.0f && point.worldY == 0.0f; });
    REQUIRE(corner != samples.end());
}

TEST_CASE("stroke input queue preserves pressure extrema", "[brush][input]")
{
    std::deque<aether::StrokeInputSample> samples { sample(0.0f, 0.0f, 0.2f, 0.000f),
        sample(4.0f, 0.0f, 0.2f, 0.004f), sample(8.0f, 0.0f, 0.9f, 0.008f),
        sample(12.0f, 0.0f, 0.2f, 0.012f), sample(16.0f, 0.0f, 0.2f, 0.016f),
        sample(20.0f, 0.0f, 0.2f, 0.020f), sample(24.0f, 0.0f, 0.2f, 0.024f),
        sample(28.0f, 0.0f, 0.2f, 0.028f), sample(32.0f, 0.0f, 0.2f, 0.032f) };

    aether::stroke_input_queue::compact(
        samples, 1.0f, aether::stroke_input_queue::queuedAgeSeconds(samples));

    const auto pressurePeak = std::find_if(samples.begin(), samples.end(),
        [](const auto& point) { return std::abs(point.pressure - 0.9f) < 0.0001f; });
    REQUIRE(pressurePeak != samples.end());
}

TEST_CASE("stroke input queue preserves speed changes used by dynamics", "[brush][input]")
{
    const auto parameters = aether::stroke_input_queue::parametersForQueueAge(0.030f);
    const auto first = sample(0.0f, 0.0f, 0.5f, 0.000f);
    const auto middle = sample(5.0f, 0.0f, 0.5f, 0.001f);
    const auto last = sample(10.0f, 0.0f, 0.5f, 0.010f);

    CHECK_FALSE(
        aether::stroke_input_queue::canRemoveMiddleSample(first, middle, last, 1.0f, parameters));
}

TEST_CASE("stroke input queue preserves reversals", "[brush][input]")
{
    const auto parameters = aether::stroke_input_queue::parametersForQueueAge(0.100f);
    const auto first = sample(0.0f, 0.0f, 0.5f, 0.000f);
    const auto middle = sample(5.0f, 0.0f, 0.5f, 0.004f);
    const auto last = sample(4.0f, 0.0f, 0.5f, 0.008f);

    CHECK_FALSE(
        aether::stroke_input_queue::canRemoveMiddleSample(first, middle, last, 1.0f, parameters));
}

TEST_CASE("stroke input queue compacts stationary packets", "[brush][input]")
{
    std::deque<aether::StrokeInputSample> samples;
    for (int index = 0; index <= 40; ++index) {
        samples.push_back(sample(12.0f, 8.0f, 0.5f, static_cast<float>(index) * 0.001f));
    }

    const std::size_t removed = aether::stroke_input_queue::compact(
        samples, 1.0f, aether::stroke_input_queue::queuedAgeSeconds(samples));

    CHECK(removed > 20);
    CHECK(samples.front().strokeElapsedSeconds == 0.0f);
    CHECK(std::abs(samples.back().strokeElapsedSeconds - 0.040f) < 0.000001f);
}

TEST_CASE("stroke input queue measures geometry in screen pixels", "[brush][input]")
{
    const auto parameters = aether::stroke_input_queue::parametersForQueueAge(0.030f);
    const auto first = sample(0.0f, 0.0f, 0.5f, 0.000f);
    const auto middle = sample(5.0f, 0.5f, 0.5f, 0.004f);
    const auto last = sample(10.0f, 0.0f, 0.5f, 0.008f);

    CHECK_FALSE(
        aether::stroke_input_queue::canRemoveMiddleSample(first, middle, last, 1.0f, parameters));
    CHECK(aether::stroke_input_queue::canRemoveMiddleSample(first, middle, last, 0.5f, parameters));
}
