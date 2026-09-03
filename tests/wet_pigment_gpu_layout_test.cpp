// SPDX-License-Identifier: MPL-2.0

#include "features/brush/rendering/WetPigmentGpuLayout.h"
#include "features/brush/rendering/WetPigmentGlsl.h"
#include "features/brush/rendering/WetShaderSources.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace aether::wet_pigment_gpu;

TEST_CASE("Wet pigment GPU layout has stable plane indices", "[pigment][gpu]")
{
    REQUIRE(index(ReservoirPlane::Pigments0) == 0);
    REQUIRE(index(ReservoirPlane::Pigments1) == 1);
    REQUIRE(index(ReservoirPlane::CorrectionAndAlpha) == 2);
    REQUIRE(index(ReservoirPlane::ColorMoments) == 3);
    REQUIRE(kReservoirPlaneCount == 4);
    REQUIRE(kReservoirPlanesAreAlphaPremultiplied);
    for (std::size_t i = 0; i < kReservoirPlaneCount; ++i) {
        REQUIRE(kReservoirAttachmentIndices[i] == static_cast<int>(i));
        REQUIRE(kReservoirOutputLocations[i] == static_cast<int>(i));
    }
}

TEST_CASE("Wet pigment GPU texture units do not overlap", "[pigment][gpu]")
{
    REQUIRE(textureUnitsAreUnique());
    REQUIRE(kReservoirTextureUnits[0] == 1);
    REQUIRE(kLutTextureUnits[0] > kReservoirTextureUnits.back());
}

TEST_CASE("Wet pigment working color never round-trips through RGBA8", "[pigment][gpu]")
{
    REQUIRE(workingColorFormat(aether::TilePixelFormat::RGBA8) == aether::TilePixelFormat::RGBA16F);
    REQUIRE(
        workingColorFormat(aether::TilePixelFormat::RGBA16F) == aether::TilePixelFormat::RGBA16F);
    REQUIRE(
        workingColorFormat(aether::TilePixelFormat::RGBA32F) == aether::TilePixelFormat::RGBA32F);
}

TEST_CASE("Wet float working color preserves muted RGB at one-byte alpha", "[pigment][gpu]")
{
    // #6D5E74 at alpha 1/255 is the reported failure case: every premultiplied
    // RGB channel rounds to zero in RGBA8 while alpha survives as one byte.
    constexpr float inv255 = 1.0f / 255.0f;
    const float straight[3] { 109.0f * inv255, 94.0f * inv255, 116.0f * inv255 };
    for (const float channel : straight) {
        const float premultiplied = channel * inv255;
        const auto rgba8Byte = static_cast<unsigned int>(premultiplied * 255.0f + 0.5f);
        REQUIRE(rgba8Byte == 0u);
        REQUIRE(aether::floatToHalfBits(premultiplied) != 0u);
    }
    REQUIRE(static_cast<unsigned int>(inv255 * 255.0f + 0.5f) == 1u);
    REQUIRE(aether::floatToHalfBits(inv255) != 0u);
}

TEST_CASE("One-byte premultiplied RGB cannot supply a stable wet-pickup hue",
    "[pigment][gpu][regression]")
{
    // The same #6D5E74 edge pixel becomes blue or magenta solely from the
    // rounding offset when alpha has only one byte of coverage. Amplifying any
    // of these straight colors in the wet reservoir therefore exposes a false
    // spectrum, not merely the previously dominant black case.
    constexpr float red = 109.0f / 255.0f;
    constexpr float green = 94.0f / 255.0f;
    constexpr float blue = 116.0f / 255.0f;
    const auto quantizedNumerator = [](float straightChannel, float roundingOffset) {
        return static_cast<int>(std::floor(straightChannel + roundingOffset));
    };

    CHECK(quantizedNumerator(red, 0.55f) == 0);
    CHECK(quantizedNumerator(green, 0.55f) == 0);
    CHECK(quantizedNumerator(blue, 0.55f) == 1); // false blue
    CHECK(quantizedNumerator(red, 0.60f) == 1);
    CHECK(quantizedNumerator(green, 0.60f) == 0);
    CHECK(quantizedNumerator(blue, 0.60f) == 1); // false magenta

    const auto fract = [](float value) { return value - std::floor(value); };
    float neighborhood[3] { 0.0f, 0.0f, 0.0f };
    constexpr float straight[3] { red, green, blue };
    constexpr float sampleCount = 49.0f;
    for (int y = 100; y <= 106; ++y) {
        for (int x = 100; x <= 106; ++x) {
            const float noise = fract(52.9829189f
                * fract(static_cast<float>(x) * 0.06711056f
                    + static_cast<float>(y) * 0.00583715f));
            for (int channel = 0; channel < 3; ++channel)
                neighborhood[channel] += std::floor(straight[channel] + noise);
        }
    }
    for (int channel = 0; channel < 3; ++channel) {
        // Normalized convolution sums premultiplied RGB and alpha before the
        // divide, recovering the hue that the dither represents spatially.
        CHECK(std::abs(neighborhood[channel] / sampleCount - straight[channel]) < 0.03f);
    }
}

TEST_CASE("Wet pigment GPU names match the shader contract", "[pigment][gpu]")
{
    REQUIRE(kReservoirSamplerNames[0] == "uReservoirPigments0");
    REQUIRE(kReservoirSamplerNames[1] == "uReservoirPigments1");
    REQUIRE(kReservoirSamplerNames[2] == "uReservoirCorrectionAndAlpha");
    REQUIRE(kReservoirSamplerNames[3] == "uReservoirColorMoments");
    REQUIRE(kReservoirOutputNames[0] == "outPigments0");
    REQUIRE(kReservoirOutputNames[3] == "outColorMoments");
    REQUIRE(kLutSamplerNames[0] == "uPigmentLut0");
    REQUIRE(kLutSamplerNames[1] == "uPigmentLut1");
    for (const auto name : kReservoirSamplerNames)
        REQUIRE(kLatentGlsl.find(name) != std::string_view::npos);
    for (const auto name : kReservoirOutputNames)
        REQUIRE(kWetPickupOutputsGlsl.find(name) != std::string_view::npos);
    for (const auto name : kLutSamplerNames)
        REQUIRE(kLatentGlsl.find(name) != std::string_view::npos);
}
