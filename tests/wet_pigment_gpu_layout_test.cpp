// SPDX-License-Identifier: MPL-2.0

#include "features/brush/rendering/WetPigmentGpuLayout.h"
#include "features/brush/rendering/WetPigmentGlsl.h"
#include "features/brush/rendering/WetShaderSources.h"

#include <catch2/catch_test_macros.hpp>

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
    REQUIRE(workingColorFormat(aether::TilePixelFormat::RGBA8)
        == aether::TilePixelFormat::RGBA16F);
    REQUIRE(workingColorFormat(aether::TilePixelFormat::RGBA16F)
        == aether::TilePixelFormat::RGBA16F);
    REQUIRE(workingColorFormat(aether::TilePixelFormat::RGBA32F)
        == aether::TilePixelFormat::RGBA32F);
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
