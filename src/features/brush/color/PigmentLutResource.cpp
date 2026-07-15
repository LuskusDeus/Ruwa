// SPDX-License-Identifier: MPL-2.0

#include "features/brush/color/PigmentLutResource.h"

#include <QFile>

#include <cstdint>
#include <span>
#include <stdexcept>

namespace ruwa::core::brushes {

PigmentLut PigmentLutResource::loadBuiltIn()
{
    QFile file(QStringLiteral(":/pigments/ruwa-pigments-v1.lut"));
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot open the built-in pigment LUT resource");
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty())
        throw std::runtime_error("The built-in pigment LUT resource is empty");
    const auto data = reinterpret_cast<const std::uint8_t*>(bytes.constData());
    return PigmentLut::deserialize(
        std::span<const std::uint8_t>(data, static_cast<std::size_t>(bytes.size())));
}

} // namespace ruwa::core::brushes
