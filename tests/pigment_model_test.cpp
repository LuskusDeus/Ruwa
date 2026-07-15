// SPDX-License-Identifier: MPL-2.0

#include "features/brush/color/PigmentModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>

using ruwa::core::brushes::PigmentModel;

namespace {

float distance(const PigmentModel::Srgb& a, const PigmentModel::Srgb& b)
{
    return std::sqrt((a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g)
        + (a.b - b.b) * (a.b - b.b));
}

float srgbToLinear(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f ? value / 12.92f
                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

std::array<float, 3> toOklab(const PigmentModel::Srgb& color)
{
    const float r = srgbToLinear(color.r);
    const float g = srgbToLinear(color.g);
    const float b = srgbToLinear(color.b);
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
        1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
        0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
}

float perceptualDistance(const PigmentModel::Srgb& a, const PigmentModel::Srgb& b)
{
    const auto first = toOklab(a);
    const auto second = toOklab(b);
    const float dl = first[0] - second[0];
    const float da = first[1] - second[1];
    const float db = first[2] - second[2];
    return std::sqrt(dl * dl + da * da + db * db);
}

} // namespace

TEST_CASE("Pigment concentrations remain normalized and finite", "[pigment]")
{
    const auto latent = PigmentModel::encode({ 0.03f, 0.8f, 0.2f });
    float total = 0.0f;
    for (const float value : latent.pigments) {
        REQUIRE(std::isfinite(value));
        REQUIRE(value >= 0.0f);
        total += value;
    }
    REQUIRE(total == Catch::Approx(1.0f).margin(1.0e-5f));
    REQUIRE(PigmentModel::isValid(latent));
}

TEST_CASE("Pigment mixing preserves endpoints and symmetry", "[pigment]")
{
    const PigmentModel::Srgb green { 3.0f / 255.0f, 254.0f / 255.0f, 14.0f / 255.0f };
    const PigmentModel::Srgb blue { 1.0f / 255.0f, 1.0f / 255.0f, 254.0f / 255.0f };

    REQUIRE(distance(PigmentModel::mix(green, blue, 0.0f), green) < 1.0e-6f);
    REQUIRE(distance(PigmentModel::mix(green, blue, 1.0f), blue) < 1.0e-6f);
    REQUIRE(distance(PigmentModel::mix(green, blue, 0.35f),
                PigmentModel::mix(blue, green, 0.65f))
        < 1.0e-5f);
}

TEST_CASE("Yellow and blue form a green-dominant mixture", "[pigment]")
{
    const PigmentModel::Srgb yellow { 1.0f, 0.9f, 0.0f };
    const PigmentModel::Srgb blue { 0.0f, 0.1f, 1.0f };
    const auto mixed = PigmentModel::mix(yellow, blue, 0.5f);
    CAPTURE(mixed.r, mixed.g, mixed.b);
    REQUIRE(mixed.g > mixed.r);
    REQUIRE(mixed.g > mixed.b);
    REQUIRE(mixed.g > 0.08f);
}

TEST_CASE("Green and blue do not collapse toward black", "[pigment][regression]")
{
    const PigmentModel::Srgb green {
        3.0f / 255.0f, 254.0f / 255.0f, 14.0f / 255.0f
    };
    const PigmentModel::Srgb blue {
        1.0f / 255.0f, 1.0f / 255.0f, 254.0f / 255.0f
    };
    const auto mixed = PigmentModel::mix(green, blue, 0.5f);
    CAPTURE(mixed.r, mixed.g, mixed.b);
    REQUIRE(std::max(mixed.r, std::max(mixed.g, mixed.b)) > 0.12f);
    REQUIRE(mixed.g > mixed.r);
    REQUIRE(mixed.b > mixed.r);
}

TEST_CASE("Small virtual-black fraction has finite physical absorption",
    "[pigment][regression]")
{
    PigmentModel::Concentrations dilutedBlack {};
    dilutedBlack[0] = 0.90f;
    dilutedBlack[1] = 0.10f;
    const auto reflectance = PigmentModel::reflectance(dilutedBlack);

    // With the old 0.0001 digital-black endpoint, just 10% black drove every
    // wavelength to about 0.002 reflectance. Re-encoding an evolving wet color
    // could introduce that amount gradually and reveal a delayed black streak.
    for (const float sample : reflectance)
        REQUIRE(sample > 0.08f);
}

TEST_CASE("Vivid orange and blue wet feedback does not accumulate black",
    "[pigment][reservoir][regression]")
{
    const PigmentModel::Srgb orange { 1.0f, 129.0f / 255.0f, 21.0f / 255.0f };
    const PigmentModel::Srgb blue { 20.0f / 255.0f, 49.0f / 255.0f, 254.0f / 255.0f };
    const auto orangeLatent = PigmentModel::encode(orange);
    const auto blueLatent = PigmentModel::encode(blue);

    // Mirrors one opaque wet texel with pickup=0.25, spread=0.2,
    // dilution=wetFlow=0 and deposit=0.2. The canvas is encoded again on every
    // dab, reproducing the feedback path that previously accumulated a dark
    // endpoint-residual bias.
    auto reservoir = PigmentModel::mix(orangeLatent, blueLatent, 0.2f);
    auto canvas = orange;
    constexpr float previousWeight = 0.75f;
    constexpr float canvasWeight = 0.20f;
    constexpr float penWeight = 0.05f;
    constexpr float deposit = 0.20f;
    for (int dab = 0; dab < 80; ++dab) {
        const auto canvasLatent = PigmentModel::encode(canvas);
        reservoir = PigmentModel::mix(
            reservoir, canvasLatent, canvasWeight / (previousWeight + canvasWeight));
        reservoir = PigmentModel::mix(reservoir, blueLatent, penWeight);

        const auto deposited = PigmentModel::decode(reservoir);
        canvas = { (1.0f - deposit) * canvas.r + deposit * deposited.r,
            (1.0f - deposit) * canvas.g + deposit * deposited.g,
            (1.0f - deposit) * canvas.b + deposit * deposited.b };
    }

    CAPTURE(canvas.r, canvas.g, canvas.b);
    REQUIRE(PigmentModel::isValid(reservoir));
    REQUIRE(std::max({ canvas.r, canvas.g, canvas.b }) > 0.35f);
}

TEST_CASE("Latent reservoir mixing does not drift after repeated reads", "[pigment][reservoir]")
{
    const auto green = PigmentModel::encode({ 0.02f, 0.95f, 0.06f });
    const auto blue = PigmentModel::encode({ 0.01f, 0.02f, 0.95f });
    const auto baseline = PigmentModel::mix(green, blue, 0.5f);
    auto stored = baseline;
    for (int iteration = 0; iteration < 1000; ++iteration)
        stored = PigmentModel::mix(stored, baseline, 0.5f);

    REQUIRE(PigmentModel::isValid(stored));
    REQUIRE(distance(PigmentModel::decode(stored), PigmentModel::decode(baseline)) < 1.0e-5f);
}

TEST_CASE("Latent accumulation is associative", "[pigment][reservoir]")
{
    const auto red = PigmentModel::encode({ 0.9f, 0.05f, 0.02f });
    const auto green = PigmentModel::encode({ 0.02f, 0.85f, 0.08f });
    const auto blue = PigmentModel::encode({ 0.03f, 0.08f, 0.95f });

    const auto firstGrouping = PigmentModel::mix(PigmentModel::mix(red, green, 0.5f), blue, 1.0f / 3.0f);
    const auto secondGrouping = PigmentModel::mix(red, PigmentModel::mix(green, blue, 0.5f), 2.0f / 3.0f);
    REQUIRE(distance(PigmentModel::decode(firstGrouping), PigmentModel::decode(secondGrouping))
        < 1.0e-5f);
}

TEST_CASE("RGB colors survive pigment encoding within the quality contract",
    "[pigment][roundtrip]")
{
    constexpr std::array<PigmentModel::Srgb, 16> colors {
        PigmentModel::Srgb { 0.0f, 0.0f, 0.0f },
        PigmentModel::Srgb { 1.0f, 1.0f, 1.0f },
        PigmentModel::Srgb { 1.0f, 0.0f, 0.0f },
        PigmentModel::Srgb { 0.0f, 1.0f, 0.0f },
        PigmentModel::Srgb { 0.0f, 0.0f, 1.0f },
        PigmentModel::Srgb { 1.0f, 1.0f, 0.0f },
        PigmentModel::Srgb { 1.0f, 0.0f, 1.0f },
        PigmentModel::Srgb { 0.0f, 1.0f, 1.0f },
        PigmentModel::Srgb { 0.5f, 0.5f, 0.5f },
        PigmentModel::Srgb { 0.25f, 0.25f, 0.25f },
        PigmentModel::Srgb { 0.75f, 0.75f, 0.75f },
        PigmentModel::Srgb { 0.03f, 0.8f, 0.2f },
        PigmentModel::Srgb { 0.9f, 0.15f, 0.04f },
        PigmentModel::Srgb { 0.35f, 0.08f, 0.75f },
        PigmentModel::Srgb { 0.95f, 0.65f, 0.15f },
        PigmentModel::Srgb { 0.12f, 0.55f, 0.82f },
    };

    float worstError = 0.0f;
    PigmentModel::Srgb worstInput {};
    PigmentModel::Srgb worstOutput {};
    for (const auto& input : colors) {
        const auto output = PigmentModel::decode(PigmentModel::encode(input));
        const float error = perceptualDistance(input, output);
        if (error > worstError) {
            worstError = error;
            worstInput = input;
            worstOutput = output;
        }
    }

    CAPTURE(worstError, worstInput.r, worstInput.g, worstInput.b, worstOutput.r, worstOutput.g,
        worstOutput.b);
    REQUIRE(worstError <= 0.08f);
}

TEST_CASE("Pigment model remains finite over the RGB cube", "[pigment][grid]")
{
    float worstRoundTripError = 0.0f;
    PigmentModel::Srgb worstInput {};
    PigmentModel::Srgb worstOutput {};
    constexpr int divisions = 8;
    for (int red = 0; red <= divisions; ++red) {
        for (int green = 0; green <= divisions; ++green) {
            for (int blue = 0; blue <= divisions; ++blue) {
                const PigmentModel::Srgb input { static_cast<float>(red) / divisions,
                    static_cast<float>(green) / divisions, static_cast<float>(blue) / divisions };
                const auto latent = PigmentModel::encode(input);
                const auto output = PigmentModel::decode(latent);
                REQUIRE(PigmentModel::isValid(latent));
                REQUIRE(std::isfinite(output.r));
                REQUIRE(std::isfinite(output.g));
                REQUIRE(std::isfinite(output.b));
                REQUIRE(output.r >= 0.0f);
                REQUIRE(output.r <= 1.0f);
                REQUIRE(output.g >= 0.0f);
                REQUIRE(output.g <= 1.0f);
                REQUIRE(output.b >= 0.0f);
                REQUIRE(output.b <= 1.0f);
                const float error = perceptualDistance(input, output);
                if (error > worstRoundTripError) {
                    worstRoundTripError = error;
                    worstInput = input;
                    worstOutput = output;
                }
            }
        }
    }

    CAPTURE(worstRoundTripError, worstInput.r, worstInput.g, worstInput.b, worstOutput.r,
        worstOutput.g, worstOutput.b);
    REQUIRE(worstRoundTripError <= 0.12f);
}
