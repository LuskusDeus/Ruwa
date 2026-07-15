// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "shared/tiles/TileFormat.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace aether::wet_pigment_gpu {

enum class ReservoirPlane : std::size_t {
    Pigments0 = 0,
    Pigments1,
    CorrectionAndAlpha,
    ColorMoments,
    Count,
};

constexpr std::size_t kReservoirPlaneCount = static_cast<std::size_t>(ReservoirPlane::Count);
constexpr std::size_t kLutTextureCount = 2;

constexpr std::array<int, kReservoirPlaneCount> kReservoirAttachmentIndices { 0, 1, 2, 3 };
constexpr std::array<int, kReservoirPlaneCount> kReservoirOutputLocations { 0, 1, 2, 3 };
constexpr std::array<std::string_view, kReservoirPlaneCount> kReservoirSamplerNames {
    "uReservoirPigments0", "uReservoirPigments1", "uReservoirCorrectionAndAlpha",
    "uReservoirColorMoments"
};
constexpr std::array<std::string_view, kReservoirPlaneCount> kReservoirOutputNames { "outPigments0",
    "outPigments1", "outCorrectionAndAlpha", "outColorMoments" };
constexpr std::array<std::string_view, kLutTextureCount> kLutSamplerNames { "uPigmentLut0",
    "uPigmentLut1" };

// WetLatent itself is straight (matching PigmentModel), but all reservoir
// payload channels are stored premultiplied by the alpha in plane 2. Sampling
// unpremultiplies after hardware filtering, so transparent texels carry no
// hidden pigment state into neighboring coverage.
inline constexpr bool kReservoirPlanesAreAlphaPremultiplied = true;

// Wet repeatedly unpremultiplies the evolving canvas while exchanging pigment
// with the reservoir. RGBA8 cannot retain the RGB of low-alpha pixels (for
// example, alpha 1/255 rounds every premultiplied channel below one byte to
// zero), so reading that pixel back would manufacture black pigment. Keep the
// complete in-progress stroke in float storage and quantize only when it is
// flattened into the document layer. Do not reduce a 32F document to 16F.
[[nodiscard]] constexpr TilePixelFormat workingColorFormat(TilePixelFormat documentFormat) noexcept
{
    return documentFormat == TilePixelFormat::RGBA32F ? TilePixelFormat::RGBA32F
                                                      : TilePixelFormat::RGBA16F;
}

// Units 0..3 are reserved for canvas, reservoir plane 0 (also used by
// smudge), selection mask and custom dab shape.
constexpr std::array<int, kReservoirPlaneCount> kReservoirTextureUnits { 1, 4, 5, 6 };
constexpr std::array<int, kLutTextureCount> kLutTextureUnits { 7, 8 };

[[nodiscard]] constexpr std::size_t index(ReservoirPlane plane) noexcept
{
    return static_cast<std::size_t>(plane);
}

[[nodiscard]] constexpr bool textureUnitsAreUnique() noexcept
{
    std::array<bool, 9> occupied {};
    for (const int unit : kReservoirTextureUnits) {
        if (unit < 0 || unit >= static_cast<int>(occupied.size()) || occupied[unit])
            return false;
        occupied[unit] = true;
    }
    for (const int unit : kLutTextureUnits) {
        if (unit < 0 || unit >= static_cast<int>(occupied.size()) || occupied[unit])
            return false;
        occupied[unit] = true;
    }
    return true;
}

static_assert(kReservoirPlaneCount == 4);
static_assert(textureUnitsAreUnique());

} // namespace aether::wet_pigment_gpu
