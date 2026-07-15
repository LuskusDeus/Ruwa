// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "features/brush/color/PigmentModel.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ruwa::core::brushes {

class PigmentLut final {
public:
    explicit PigmentLut(std::size_t size);

    [[nodiscard]] static PigmentLut generate(std::size_t size);
    [[nodiscard]] static PigmentLut deserialize(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] const std::vector<PigmentModel::Concentrations>& entries() const noexcept
    {
        return m_entries;
    }
    [[nodiscard]] PigmentModel::Latent sample(const PigmentModel::Srgb& color) const;
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;

private:
    [[nodiscard]] std::size_t index(
        std::size_t red, std::size_t green, std::size_t blue) const noexcept;

    std::size_t m_size;
    std::vector<PigmentModel::Concentrations> m_entries;
};

} // namespace ruwa::core::brushes
