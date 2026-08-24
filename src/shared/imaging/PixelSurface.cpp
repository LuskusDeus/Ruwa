// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A R E D   |   P I X E L   S U R F A C E
// ==========================================================================

#include "shared/imaging/PixelSurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace ruwa::shared::imaging {

qsizetype PixelSurface::byteSizeFor(int width, int height, PixelStorage storage)
{
    if (width <= 0 || height <= 0) {
        return 0;
    }

    const qsizetype bpp = storageBytesPerPixel(storage);
    const qsizetype maxV = std::numeric_limits<qsizetype>::max();

    // Overflow-checked width * bpp * height. A 32-bit qsizetype build would
    // wrap on frames that are merely large rather than absurd, and the wrapped
    // value would size a buffer the row loop then writes past.
    if (static_cast<qsizetype>(width) > maxV / bpp) {
        return 0;
    }
    const qsizetype bytesPerLine = static_cast<qsizetype>(width) * bpp;
    if (bytesPerLine > maxV / static_cast<qsizetype>(height)) {
        return 0;
    }
    return bytesPerLine * static_cast<qsizetype>(height);
}

PixelSurface PixelSurface::create(int width, int height, PixelStorage storage, PixelAlpha alpha)
{
    const qsizetype totalBytes = byteSizeFor(width, height, storage);
    if (totalBytes <= 0) {
        return {};
    }

    PixelSurface surface;
    try {
        surface.m_data.resize(static_cast<size_t>(totalBytes));
    } catch (const std::bad_alloc&) {
        return {};
    } catch (const std::length_error&) {
        return {};
    }

    surface.m_width = width;
    surface.m_height = height;
    surface.m_storage = storage;
    surface.m_alpha = alpha;
    surface.m_bytesPerLine = static_cast<qsizetype>(width) * storageBytesPerPixel(storage);
    return surface;
}

void PixelSurface::readRowFloat(int y, float* dst) const
{
    if (isNull() || y < 0 || y >= m_height || dst == nullptr) {
        return;
    }

    const uint8_t* row = scanLine(y);
    const qsizetype componentCount = static_cast<qsizetype>(m_width) * kPixelChannels;

    if (m_storage == PixelStorage::Float32) {
        std::memcpy(dst, row, static_cast<size_t>(componentCount) * sizeof(float));
        return;
    }

    constexpr float inv = 1.0f / 255.0f;
    for (qsizetype i = 0; i < componentCount; ++i) {
        dst[i] = static_cast<float>(row[i]) * inv;
    }
}

void PixelSurface::writeRowFloat(int y, const float* src)
{
    if (isNull() || y < 0 || y >= m_height || src == nullptr) {
        return;
    }

    uint8_t* row = scanLine(y);
    const qsizetype componentCount = static_cast<qsizetype>(m_width) * kPixelChannels;

    if (m_storage == PixelStorage::Float32) {
        std::memcpy(row, src, static_cast<size_t>(componentCount) * sizeof(float));
        return;
    }

    for (qsizetype i = 0; i < componentCount; ++i) {
        const float scaled = src[i] * 255.0f;
        const float clamped = std::clamp(scaled, 0.0f, 255.0f);
        row[i] = static_cast<uint8_t>(std::lround(clamped));
    }
}

namespace {

/// table[a * 256 + c] = round(c * 255 / a), saturated. Row a == 0 stays zero:
/// a fully transparent premultiplied pixel carries no color to recover.
const std::array<uint8_t, 256 * 256>& unpremultiplyTable()
{
    static const std::array<uint8_t, 256 * 256> table = [] {
        std::array<uint8_t, 256 * 256> t {};
        for (int a = 1; a < 256; ++a) {
            for (int c = 0; c < 256; ++c) {
                const int value = (c * 255 + a / 2) / a;
                t[static_cast<size_t>(a) * 256 + c] = static_cast<uint8_t>(std::min(255, value));
            }
        }
        return t;
    }();
    return table;
}

} // anonymous namespace

void PixelSurface::convertAlphaMode(PixelAlpha target)
{
    if (isNull() || m_alpha == target) {
        m_alpha = target;
        return;
    }

    const bool toStraight = target == PixelAlpha::Straight;

    if (m_storage == PixelStorage::UInt8) {
        const auto& table = unpremultiplyTable();
        for (int y = 0; y < m_height; ++y) {
            uint8_t* row = scanLine(y);
            for (int x = 0; x < m_width; ++x) {
                uint8_t* p = row + static_cast<qsizetype>(x) * kPixelChannels;
                const uint8_t a = p[3];
                if (toStraight) {
                    const size_t base = static_cast<size_t>(a) * 256;
                    p[0] = table[base + p[0]];
                    p[1] = table[base + p[1]];
                    p[2] = table[base + p[2]];
                } else {
                    p[0] = static_cast<uint8_t>((p[0] * a + 127) / 255);
                    p[1] = static_cast<uint8_t>((p[1] * a + 127) / 255);
                    p[2] = static_cast<uint8_t>((p[2] * a + 127) / 255);
                }
            }
        }
    } else {
        for (int y = 0; y < m_height; ++y) {
            auto* row = reinterpret_cast<float*>(scanLine(y));
            for (int x = 0; x < m_width; ++x) {
                float* p = row + static_cast<qsizetype>(x) * kPixelChannels;
                const float a = std::clamp(p[3], 0.0f, 1.0f);
                if (toStraight) {
                    if (a <= 0.0f) {
                        p[0] = 0.0f;
                        p[1] = 0.0f;
                        p[2] = 0.0f;
                    } else {
                        const float inv = 1.0f / a;
                        p[0] = std::max(0.0f, p[0]) * inv;
                        p[1] = std::max(0.0f, p[1]) * inv;
                        p[2] = std::max(0.0f, p[2]) * inv;
                    }
                } else {
                    p[0] *= a;
                    p[1] *= a;
                    p[2] *= a;
                }
                p[3] = a;
            }
        }
    }

    m_alpha = target;
}

PixelSurface PixelSurface::convertedTo(PixelStorage storage) const
{
    if (isNull()) {
        return {};
    }

    PixelSurface out = create(m_width, m_height, storage, m_alpha);
    if (out.isNull()) {
        return {};
    }

    if (storage == m_storage) {
        std::memcpy(out.m_data.data(), m_data.data(), m_data.size());
        return out;
    }

    std::vector<float> row(static_cast<size_t>(m_width) * kPixelChannels);
    for (int y = 0; y < m_height; ++y) {
        readRowFloat(y, row.data());
        out.writeRowFloat(y, row.data());
    }
    return out;
}

} // namespace ruwa::shared::imaging
