// SPDX-License-Identifier: MPL-2.0

#include "features/brush/color/PigmentModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ruwa::core::brushes {
namespace {

using Spectrum = PigmentModel::Spectrum;
using Concentrations = PigmentModel::Concentrations;
using Srgb = PigmentModel::Srgb;
using LinearRgb = PigmentModel::LinearRgb;
using Latent = PigmentModel::Latent;

constexpr float kMinimumReflectance = 1.0e-4f;
// A paint pigment is not a digital zero. Carbon-black masstones typically
// retain a small diffuse reflectance; keeping that finite prevents a few
// percent of reconstructed black from dominating every other pigment through
// the Kubelka-Munk K/S transform. Exact RGB black is still preserved by the
// endpoint colorMean anchor in decode().
constexpr float kBlackPigmentReflectance = 0.018f;

// Reflectance curves for Ruwa's virtual artist pigments, sampled uniformly at
// 380..730 nm. They are generated from smooth analytic absorption bands rather
// than reconstructed from the input RGB color. This keeps every K/S value
// finite and makes the latent pigment state safe to accumulate.
const std::array<Spectrum, PigmentModel::kPigmentCount>& pigmentReflectances()
{
    static const auto spectra = [] {
        std::array<Spectrum, PigmentModel::kPigmentCount> result {};
        for (std::size_t sample = 0; sample < PigmentModel::kSpectralSampleCount; ++sample) {
            const float wavelength = 380.0f
                + 350.0f * static_cast<float>(sample)
                    / static_cast<float>(PigmentModel::kSpectralSampleCount - 1);
            const auto gaussian = [wavelength](float center, float width) {
                const float x = (wavelength - center) / width;
                return std::exp(-0.5f * x * x);
            };
            const auto logistic = [wavelength](float center, float width) {
                return 1.0f / (1.0f + std::exp(-(wavelength - center) / width));
            };

            result[0][sample] = 0.94f; // white
            result[1][sample] = kBlackPigmentReflectance; // black
            result[2][sample] = 0.035f + 0.90f * logistic(585.0f, 18.0f); // red
            result[3][sample] = 0.025f + 0.88f * gaussian(535.0f, 43.0f); // green
            // Blue must retain a narrow short-wavelength lobe so the model can
            // reach the sRGB blue corner without an unavoidable green cast.
            result[4][sample] = 0.025f + 0.86f * gaussian(455.0f, 40.0f); // blue
            // The yellow absorption edge rejects the blue lobe while retaining
            // reflectance through green and red. Its overlap with the falling
            // cyan shoulder of blue therefore resolves to a green secondary.
            result[5][sample] = 0.035f + 0.89f * logistic(500.0f, 17.0f); // yellow
            result[6][sample] = 0.91f - 0.84f * gaussian(545.0f, 48.0f); // magenta
            result[7][sample] = 0.91f - 0.86f * logistic(590.0f, 22.0f); // cyan
        }
        return result;
    }();
    return spectra;
}

constexpr std::array<float, PigmentModel::kSpectralSampleCount> kXBar { 0.0002f, 0.0082f, 0.1285f,
    0.3362f, 0.2908f, 0.0956f, 0.0049f, 0.0633f, 0.2904f, 0.5945f, 0.9163f, 1.0559f, 0.8544f,
    0.4464f, 0.1649f, 0.0468f };
constexpr std::array<float, PigmentModel::kSpectralSampleCount> kYBar { 0.0003f, 0.0024f, 0.0258f,
    0.0937f, 0.1813f, 0.3362f, 0.6182f, 0.8800f, 0.9918f, 0.9355f, 0.7100f, 0.4508f, 0.2383f,
    0.1016f, 0.0341f, 0.0087f };
constexpr std::array<float, PigmentModel::kSpectralSampleCount> kZBar { 0.0067f, 0.0380f, 0.3856f,
    1.3898f, 1.7074f, 0.8129f, 0.2720f, 0.0822f, 0.0203f, 0.0045f, 0.0010f, 0.0002f, 0.0000f,
    0.0000f, 0.0000f, 0.0000f };
constexpr std::array<float, PigmentModel::kSpectralSampleCount> kD65 { 49.98f, 82.75f, 93.43f,
    104.87f, 117.81f, 115.92f, 109.35f, 104.79f, 104.41f, 100.00f, 95.79f, 90.01f, 87.70f, 83.70f,
    80.21f, 78.28f };

float linearToSrgb(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

float srgbToLinear(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

Concentrations normalized(Concentrations values)
{
    float total = 0.0f;
    for (float& value : values) {
        value = std::max(value, 0.0f);
        total += value;
    }
    if (total <= std::numeric_limits<float>::epsilon()) {
        values.fill(0.0f);
        values[0] = 1.0f;
        return values;
    }
    for (float& value : values)
        value /= total;
    return values;
}

float colorError(const Srgb& a, const Srgb& b)
{
    const float dr = a.r - b.r;
    const float dg = a.g - b.g;
    const float db = a.b - b.b;
    return dr * dr + 1.5f * dg * dg + db * db;
}

LinearRgb decodePigmentsLinear(const Concentrations& concentrations)
{
    const Spectrum rho = PigmentModel::reflectance(concentrations);
    float normalization = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    for (std::size_t i = 0; i < PigmentModel::kSpectralSampleCount; ++i) {
        normalization += kYBar[i] * kD65[i];
        x += kXBar[i] * kD65[i] * rho[i];
        y += kYBar[i] * kD65[i] * rho[i];
        z += kZBar[i] * kD65[i] * rho[i];
    }
    x /= normalization;
    y /= normalization;
    z /= normalization;

    return { 3.2406255f * x - 1.5372080f * y - 0.4986286f * z,
        -0.9689307f * x + 1.8757561f * y + 0.0415175f * z,
        0.0557101f * x - 0.2040211f * y + 1.0569959f * z };
}

Srgb decodePigments(const Concentrations& concentrations)
{
    const LinearRgb linear = decodePigmentsLinear(concentrations);
    return { linearToSrgb(linear.r), linearToSrgb(linear.g), linearToSrgb(linear.b) };
}

float encodingObjective(const Concentrations& concentrations, const Srgb& target)
{
    const float chroma
        = std::max({ target.r, target.g, target.b }) - std::min({ target.r, target.g, target.b });
    constexpr float kChromaticBlackRegularization = 1.0f;

    // Eight pigment weights encode only three visible channels, so many very
    // different pigment sets are near-identical RGB metamers. Without a
    // canonical preference, adjacent LUT nodes can alternate between a
    // chromatic solution and 90% virtual black. Trilinear sampling then injects
    // real black into a smoothly varying color and repeated pickup amplifies it.
    // Prefer chromatic pigments for chromatic targets; the factor naturally
    // vanishes on the neutral axis, where virtual black remains meaningful.
    const float virtualBlack = concentrations[1];
    return colorError(decodePigments(concentrations), target)
        + kChromaticBlackRegularization * chroma * virtualBlack * virtualBlack;
}

} // namespace

PigmentModel::Spectrum PigmentModel::reflectance(const Concentrations& concentrations)
{
    const Concentrations weights = normalized(concentrations);
    Spectrum result {};
    const auto& pigments = pigmentReflectances();
    for (std::size_t wavelength = 0; wavelength < kSpectralSampleCount; ++wavelength) {
        float ks = 0.0f;
        for (std::size_t pigment = 0; pigment < kPigmentCount; ++pigment) {
            const float rho = std::clamp(pigments[pigment][wavelength], kMinimumReflectance, 1.0f);
            ks += weights[pigment] * (1.0f - rho) * (1.0f - rho) / (2.0f * rho);
        }
        result[wavelength]
            = std::clamp(1.0f + ks - std::sqrt(ks * ks + 2.0f * ks), kMinimumReflectance, 1.0f);
    }
    return result;
}

PigmentModel::Srgb PigmentModel::decode(const Latent& latent)
{
    const LinearRgb base = decodePigmentsLinear(latent.pigments);
    const float meanSquared = latent.colorMean.r * latent.colorMean.r
        + latent.colorMean.g * latent.colorMean.g + latent.colorMean.b * latent.colorMean.b;
    const float variance = std::max(latent.colorSecondMoment - meanSquared, 0.0f);
    constexpr float kEndpointVarianceFalloff = 8.0f;
    const float endpointWeight = std::exp(-kEndpointVarianceFalloff * variance);

    // Spectral decoding is nonlinear, therefore endpoint residuals cannot be
    // averaged and applied to the decoded mixture. Doing that repeatedly can
    // subtract energy from every channel and eventually collapse vivid mixes
    // toward black. colorMean is affine under mixing, so it is the valid color
    // anchor: exact for an unmixed endpoint and progressively released as the
    // mixture gains color variance.
    return { linearToSrgb(base.r + endpointWeight * (latent.colorMean.r - base.r)),
        linearToSrgb(base.g + endpointWeight * (latent.colorMean.g - base.g)),
        linearToSrgb(base.b + endpointWeight * (latent.colorMean.b - base.b)) };
}

PigmentModel::Latent PigmentModel::encode(const Srgb& input)
{
    const Srgb target { std::clamp(input.r, 0.0f, 1.0f), std::clamp(input.g, 0.0f, 1.0f),
        std::clamp(input.b, 0.0f, 1.0f) };
    const float maximum = std::max({ target.r, target.g, target.b });
    const float minimum = std::min({ target.r, target.g, target.b });
    Concentrations result {};
    result[0] = minimum;
    result[1] = 1.0f - maximum;
    result[2] = std::max(target.r - std::max(target.g, target.b), 0.0f);
    result[3] = std::max(target.g - std::max(target.r, target.b), 0.0f);
    result[4] = std::max(target.b - std::max(target.r, target.g), 0.0f);
    result[5] = std::max(std::min(target.r, target.g) - target.b, 0.0f);
    result[6] = std::max(std::min(target.r, target.b) - target.g, 0.0f);
    result[7] = std::max(std::min(target.g, target.b) - target.r, 0.0f);
    result = normalized(result);

    // Small projected coordinate search. The future GPU path samples the
    // generated LUT, so this optimizer remains an offline/CPU operation.
    float step = 0.25f;
    float bestError = encodingObjective(result, target);
    constexpr float kMinimumSearchStep = 1.0e-5f;
    for (int pass = 0; pass < 40 && step >= kMinimumSearchStep; ++pass) {
        bool improved = false;
        for (std::size_t from = 0; from < kPigmentCount; ++from) {
            for (std::size_t to = 0; to < kPigmentCount; ++to) {
                if (from == to || result[from] <= 0.0f)
                    continue;
                Concentrations candidate = result;
                const float transfer = std::min(step, candidate[from]);
                candidate[from] -= transfer;
                candidate[to] += transfer;
                const float error = encodingObjective(candidate, target);
                if (error < bestError) {
                    result = candidate;
                    bestError = error;
                    improved = true;
                }
            }
        }
        if (!improved)
            step *= 0.5f;
    }
    return fromPigments(target, result);
}

PigmentModel::Latent PigmentModel::fromPigments(
    const Srgb& input, const Concentrations& concentrations)
{
    const Srgb target { std::clamp(input.r, 0.0f, 1.0f), std::clamp(input.g, 0.0f, 1.0f),
        std::clamp(input.b, 0.0f, 1.0f) };
    const Concentrations result = normalized(concentrations);
    const LinearRgb targetLinear { srgbToLinear(target.r), srgbToLinear(target.g),
        srgbToLinear(target.b) };
    return { result, {}, targetLinear,
        targetLinear.r * targetLinear.r + targetLinear.g * targetLinear.g
            + targetLinear.b * targetLinear.b };
}

PigmentModel::Latent PigmentModel::mix(const Latent& first, const Latent& second, float amount)
{
    const Concentrations a = normalized(first.pigments);
    const Concentrations b = normalized(second.pigments);
    const float t = std::clamp(amount, 0.0f, 1.0f);
    Concentrations result {};
    for (std::size_t i = 0; i < kPigmentCount; ++i)
        result[i] = (1.0f - t) * a[i] + t * b[i];
    return { normalized(result), {},
        { (1.0f - t) * first.colorMean.r + t * second.colorMean.r,
            (1.0f - t) * first.colorMean.g + t * second.colorMean.g,
            (1.0f - t) * first.colorMean.b + t * second.colorMean.b },
        (1.0f - t) * first.colorSecondMoment + t * second.colorSecondMoment };
}

PigmentModel::Srgb PigmentModel::mix(const Srgb& first, const Srgb& second, float amount)
{
    if (amount <= 0.0f)
        return first;
    if (amount >= 1.0f)
        return second;
    return decode(mix(encode(first), encode(second), amount));
}

bool PigmentModel::isValid(const Latent& latent)
{
    float total = 0.0f;
    for (const float value : latent.pigments) {
        if (!std::isfinite(value) || value < 0.0f)
            return false;
        total += value;
    }
    return std::isfinite(total) && total > 0.0f && std::isfinite(latent.correction.r)
        && std::isfinite(latent.correction.g) && std::isfinite(latent.correction.b)
        && std::isfinite(latent.colorMean.r) && std::isfinite(latent.colorMean.g)
        && std::isfinite(latent.colorMean.b) && std::isfinite(latent.colorSecondMoment)
        && latent.colorSecondMoment >= 0.0f;
}

} // namespace ruwa::core::brushes
