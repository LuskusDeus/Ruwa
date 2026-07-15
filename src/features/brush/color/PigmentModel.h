// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <array>
#include <cstddef>

namespace ruwa::core::brushes {

class PigmentModel final {
public:
    static constexpr std::size_t kSpectralSampleCount = 16;
    static constexpr std::size_t kPigmentCount = 8;

    using Spectrum = std::array<float, kSpectralSampleCount>;
    using Concentrations = std::array<float, kPigmentCount>;

    struct Srgb {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };

    struct LinearRgb {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };

    struct Latent {
        Concentrations pigments {};
        // Reserved to keep the established CPU/GPU latent layout stable.
        // Endpoint residuals are not valid after nonlinear spectral mixing.
        LinearRgb correction {};
        LinearRgb colorMean {};
        float colorSecondMoment = 0.0f;
    };

    [[nodiscard]] static Latent encode(const Srgb& color);
    [[nodiscard]] static Latent fromPigments(
        const Srgb& color, const Concentrations& concentrations);
    [[nodiscard]] static Srgb decode(const Latent& latent);
    [[nodiscard]] static Latent mix(const Latent& first, const Latent& second, float amount);
    [[nodiscard]] static Srgb mix(const Srgb& first, const Srgb& second, float amount);
    [[nodiscard]] static Spectrum reflectance(const Concentrations& concentrations);
    [[nodiscard]] static bool isValid(const Latent& latent);
};

} // namespace ruwa::core::brushes
