// SPDX-License-Identifier: MPL-2.0

#include "features/brush/color/PigmentLut.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ruwa::core::brushes {
namespace {

struct AxisSample {
    std::size_t lower;
    std::size_t upper;
    float amount;
};

constexpr std::array<std::uint8_t, 8> kMagic { 'R', 'U', 'W', 'A', 'P', 'G', 'L', 'T' };
// Version 3 uses the finite-reflectance black pigment model. Version 2 tables
// were optimized against an almost perfectly absorbing digital-black endpoint;
// even small black concentrations from a later RGB re-encode could therefore
// collapse a wet mixture and must not be consumed by the current GPU path.
constexpr std::uint32_t kFormatVersion = 3;
constexpr std::size_t kHeaderSize = kMagic.size() + 4 + 4 + 4 + 4;

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 24));
}

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    if (offset + 4 > bytes.size())
        throw std::invalid_argument("PigmentLut data is truncated");
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint32_t checksum(std::span<const std::uint8_t> bytes)
{
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

std::size_t entryCountForSize(std::size_t size)
{
    if (size < 2)
        throw std::invalid_argument("PigmentLut size must be at least two");
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (size > maximum / size)
        throw std::length_error("PigmentLut dimensions overflow");
    const std::size_t square = size * size;
    if (square > maximum / size)
        throw std::length_error("PigmentLut dimensions overflow");
    return square * size;
}

AxisSample sampleAxis(float value, std::size_t size)
{
    const float position = std::clamp(value, 0.0f, 1.0f) * static_cast<float>(size - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    return { lower, std::min(lower + 1, size - 1), position - static_cast<float>(lower) };
}

} // namespace

PigmentLut::PigmentLut(std::size_t size)
    : m_size(size)
    , m_entries(entryCountForSize(size))
{
}

PigmentLut PigmentLut::generate(std::size_t size)
{
    PigmentLut lut(size);
    const float denominator = static_cast<float>(size - 1);
    for (std::size_t red = 0; red < size; ++red) {
        for (std::size_t green = 0; green < size; ++green) {
            for (std::size_t blue = 0; blue < size; ++blue) {
                const PigmentModel::Srgb color { static_cast<float>(red) / denominator,
                    static_cast<float>(green) / denominator,
                    static_cast<float>(blue) / denominator };
                lut.m_entries[lut.index(red, green, blue)] = PigmentModel::encode(color).pigments;
            }
        }
    }
    return lut;
}

PigmentLut PigmentLut::deserialize(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        throw std::invalid_argument("PigmentLut data has an invalid signature");
    }
    const std::uint32_t version = readU32(bytes, 8);
    const std::uint32_t size = readU32(bytes, 12);
    const std::uint32_t pigmentCount = readU32(bytes, 16);
    const std::uint32_t storedChecksum = readU32(bytes, 20);
    if (version != kFormatVersion)
        throw std::invalid_argument("PigmentLut data has an unsupported version");
    if (size < 2 || pigmentCount != PigmentModel::kPigmentCount)
        throw std::invalid_argument("PigmentLut data has invalid dimensions");

    const std::size_t entryCount = entryCountForSize(size);
    if (entryCount > std::numeric_limits<std::size_t>::max() / PigmentModel::kPigmentCount / 4)
        throw std::invalid_argument("PigmentLut dimensions overflow");
    const std::size_t payloadSize = entryCount * PigmentModel::kPigmentCount * 4;
    if (bytes.size() != kHeaderSize + payloadSize)
        throw std::invalid_argument("PigmentLut data has an invalid payload size");
    const auto payload = bytes.subspan(kHeaderSize);
    if (checksum(payload) != storedChecksum)
        throw std::invalid_argument("PigmentLut checksum mismatch");

    PigmentLut lut(size);
    std::size_t offset = 0;
    for (auto& entry : lut.m_entries) {
        float total = 0.0f;
        for (float& concentration : entry) {
            concentration = std::bit_cast<float>(readU32(payload, offset));
            offset += 4;
            if (!std::isfinite(concentration) || concentration < 0.0f)
                throw std::invalid_argument("PigmentLut contains an invalid concentration");
            total += concentration;
        }
        if (std::abs(total - 1.0f) > 1.0e-4f)
            throw std::invalid_argument("PigmentLut contains a non-normalized entry");
    }
    return lut;
}

PigmentModel::Latent PigmentLut::sample(const PigmentModel::Srgb& color) const
{
    const AxisSample red = sampleAxis(color.r, m_size);
    const AxisSample green = sampleAxis(color.g, m_size);
    const AxisSample blue = sampleAxis(color.b, m_size);
    PigmentModel::Concentrations result {};

    for (std::size_t redCorner = 0; redCorner < 2; ++redCorner) {
        const std::size_t redIndex = redCorner == 0 ? red.lower : red.upper;
        const float redWeight = redCorner == 0 ? 1.0f - red.amount : red.amount;
        for (std::size_t greenCorner = 0; greenCorner < 2; ++greenCorner) {
            const std::size_t greenIndex = greenCorner == 0 ? green.lower : green.upper;
            const float greenWeight = greenCorner == 0 ? 1.0f - green.amount : green.amount;
            for (std::size_t blueCorner = 0; blueCorner < 2; ++blueCorner) {
                const std::size_t blueIndex = blueCorner == 0 ? blue.lower : blue.upper;
                const float blueWeight = blueCorner == 0 ? 1.0f - blue.amount : blue.amount;
                const float weight = redWeight * greenWeight * blueWeight;
                const auto& corner = m_entries[index(redIndex, greenIndex, blueIndex)];
                for (std::size_t pigment = 0; pigment < PigmentModel::kPigmentCount; ++pigment)
                    result[pigment] += weight * corner[pigment];
            }
        }
    }
    return PigmentModel::fromPigments(color, result);
}

std::vector<std::uint8_t> PigmentLut::serialize() const
{
    if (m_size > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("PigmentLut is too large to serialize");
    std::vector<std::uint8_t> payload;
    payload.reserve(m_entries.size() * PigmentModel::kPigmentCount * 4);
    for (const auto& entry : m_entries) {
        for (const float concentration : entry)
            appendU32(payload, std::bit_cast<std::uint32_t>(concentration));
    }

    std::vector<std::uint8_t> output;
    output.reserve(kHeaderSize + payload.size());
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    appendU32(output, kFormatVersion);
    appendU32(output, static_cast<std::uint32_t>(m_size));
    appendU32(output, static_cast<std::uint32_t>(PigmentModel::kPigmentCount));
    appendU32(output, checksum(payload));
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::size_t PigmentLut::index(std::size_t red, std::size_t green, std::size_t blue) const noexcept
{
    return (red * m_size + green) * m_size + blue;
}

} // namespace ruwa::core::brushes
