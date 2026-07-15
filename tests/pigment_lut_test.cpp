// SPDX-License-Identifier: MPL-2.0

#include "features/brush/color/PigmentLut.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

using ruwa::core::brushes::PigmentLut;
using ruwa::core::brushes::PigmentModel;

namespace {

float distance(const PigmentModel::Srgb& first, const PigmentModel::Srgb& second)
{
    const float red = first.r - second.r;
    const float green = first.g - second.g;
    const float blue = first.b - second.b;
    return std::sqrt(red * red + green * green + blue * blue);
}

} // namespace

TEST_CASE("Pigment LUT preserves exact grid nodes", "[pigment][lut]")
{
    const auto lut = PigmentLut::generate(5);
    constexpr float denominator = 4.0f;
    for (int red = 0; red < 5; ++red) {
        for (int green = 0; green < 5; ++green) {
            for (int blue = 0; blue < 5; ++blue) {
                const PigmentModel::Srgb color { red / denominator, green / denominator,
                    blue / denominator };
                const auto direct = PigmentModel::encode(color);
                const auto sampled = lut.sample(color);
                for (std::size_t pigment = 0; pigment < PigmentModel::kPigmentCount; ++pigment)
                    REQUIRE(sampled.pigments[pigment]
                        == Catch::Approx(direct.pigments[pigment]).margin(1.0e-6f));
            }
        }
    }
}

TEST_CASE("Pigment LUT interpolation preserves selected RGB", "[pigment][lut]")
{
    const auto lut = PigmentLut::generate(9);
    constexpr std::array<PigmentModel::Srgb, 5> colors {
        PigmentModel::Srgb { 0.03f, 0.8f, 0.2f },
        PigmentModel::Srgb { 0.91f, 0.17f, 0.04f },
        PigmentModel::Srgb { 0.35f, 0.08f, 0.75f },
        PigmentModel::Srgb { 0.12f, 0.55f, 0.82f },
        PigmentModel::Srgb { 0.37f, 0.37f, 0.37f },
    };
    for (const auto& color : colors) {
        const auto latent = lut.sample(color);
        REQUIRE(PigmentModel::isValid(latent));
        REQUIRE(distance(PigmentModel::decode(latent), color) < 1.0e-5f);
    }
}

TEST_CASE("Pigment LUT retains painterly key mixtures", "[pigment][lut][regression]")
{
    const auto lut = PigmentLut::generate(9);
    const auto yellow = lut.sample({ 1.0f, 0.9f, 0.0f });
    const auto blue = lut.sample({ 0.0f, 0.1f, 1.0f });
    const auto mixed = PigmentModel::decode(PigmentModel::mix(yellow, blue, 0.5f));
    CAPTURE(mixed.r, mixed.g, mixed.b);
    REQUIRE(mixed.g > mixed.r);
    REQUIRE(mixed.g > mixed.b);
}

TEST_CASE("Pigment LUT binary format round-trips exactly", "[pigment][lut][serialization]")
{
    const auto original = PigmentLut::generate(5);
    const auto bytes = original.serialize();
    const auto restored = PigmentLut::deserialize(bytes);
    REQUIRE(restored.size() == original.size());
    REQUIRE(restored.entries() == original.entries());
}

TEST_CASE("Pigment LUT rejects corrupted binary data", "[pigment][lut][serialization]")
{
    const auto lut = PigmentLut::generate(3);
    auto bytes = lut.serialize();
    bytes.back() ^= 0x01u;
    REQUIRE_THROWS_AS(PigmentLut::deserialize(bytes), std::invalid_argument);
    bytes.resize(12);
    REQUIRE_THROWS_AS(PigmentLut::deserialize(bytes), std::invalid_argument);
}

TEST_CASE("Pigment LUT rejects pre-finite-black resources",
    "[pigment][lut][serialization][regression]")
{
    const auto lut = PigmentLut::generate(3);
    auto bytes = lut.serialize();
    bytes[8] = 2u;
    bytes[9] = 0u;
    bytes[10] = 0u;
    bytes[11] = 0u;
    REQUIRE_THROWS_AS(PigmentLut::deserialize(bytes), std::invalid_argument);
}
