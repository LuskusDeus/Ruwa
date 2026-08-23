// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A R E D   |   P I X E L   S U R F A C E
// ==========================================================================
//
//   A plain CPU raster of straight-alpha RGBA pixels, row 0 at the top.
//
//   This is the transport format between the GPU readback (which must run on
//   the GUI thread, because that is where the GL context lives) and everything
//   that happens afterwards — resampling, depth conversion, encoding — which
//   must NOT. It therefore deliberately knows nothing about Qt widgets, GL, or
//   the document: it is a movable value that a worker thread can own outright.
//
//   Two storages exist because the document tile format does:
//     - UInt8   : an 8-bit document reads back byte-exact, no conversion cost
//     - Float32 : a 16F/32F document keeps its precision through the pipeline
//   Half-float is not a storage here on purpose. The readback asks the driver
//   for GL_FLOAT out of a 16F attachment and lets it widen, which costs one
//   conversion in the driver instead of a hand-rolled half decoder in every
//   consumer.
//
//   ALPHA MODE travels with the surface instead of being an unwritten
//   assumption. The GPU readback arrives PREMULTIPLIED (the tile blend folds
//   color to C*a), image files store STRAIGHT, and filtering is only correct on
//   premultiplied data. Tracking the mode lets the pipeline convert exactly
//   once, at the end, instead of dividing and re-multiplying at every hand-off
//   and losing a little precision on each round trip.
//

#ifndef RUWA_SHARED_IMAGING_PIXELSURFACE_H
#define RUWA_SHARED_IMAGING_PIXELSURFACE_H

#include <QSize>

#include <cstdint>
#include <vector>

namespace ruwa::shared::imaging {

enum class PixelAlpha : uint8_t {
    Straight = 0, ///< Color is independent of alpha. What image files store.
    Premultiplied = 1, ///< Color already scaled by alpha. What the GPU produces
                       ///< and the only space in which filtering is correct.
};

enum class PixelStorage : uint8_t {
    UInt8 = 0, ///< 4 bytes per pixel, value/255 normalization
    Float32 = 1, ///< 16 bytes per pixel, values nominally in [0,1]
};

constexpr int kPixelChannels = 4;

constexpr qsizetype storageBytesPerChannel(PixelStorage storage)
{
    return storage == PixelStorage::Float32 ? 4 : 1;
}

constexpr qsizetype storageBytesPerPixel(PixelStorage storage)
{
    return storageBytesPerChannel(storage) * kPixelChannels;
}

class PixelSurface {
public:
    PixelSurface() = default;

    /// Allocate a surface. Returns a null surface when the size is invalid or
    /// the allocation fails — an export of a 20000x20000 frame is a plausible
    /// user action, so out-of-memory is an expected outcome to report, not a
    /// crash to let through.
    [[nodiscard]] static PixelSurface create(
        int width, int height, PixelStorage storage, PixelAlpha alpha = PixelAlpha::Straight);

    /// Byte size an allocation of this shape would need, without allocating.
    /// Returns 0 when the product overflows qsizetype.
    [[nodiscard]] static qsizetype byteSizeFor(int width, int height, PixelStorage storage);

    [[nodiscard]] bool isNull() const { return m_width <= 0 || m_height <= 0 || m_data.empty(); }
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] QSize size() const { return QSize(m_width, m_height); }
    [[nodiscard]] PixelStorage storage() const { return m_storage; }
    [[nodiscard]] PixelAlpha alphaMode() const { return m_alpha; }

    [[nodiscard]] qsizetype bytesPerLine() const { return m_bytesPerLine; }

    /// Convert pixels in place. A no-op when the mode already matches. The
    /// 8-bit direction goes through a 64 KB lookup table because the naive form
    /// is a divide per channel, and an export is hundreds of millions of them.
    void convertAlphaMode(PixelAlpha target);

    [[nodiscard]] uint8_t* bits() { return m_data.data(); }
    [[nodiscard]] uint8_t* scanLine(int y) { return m_data.data() + y * m_bytesPerLine; }
    [[nodiscard]] const uint8_t* scanLine(int y) const { return m_data.data() + y * m_bytesPerLine; }

    /// Row access in a storage-independent form. `dst` / `src` hold
    /// width() * 4 floats. UInt8 rows are normalized by 1/255 on read and
    /// rounded and clamped on write, so a round trip is lossless.
    void readRowFloat(int y, float* dst) const;
    void writeRowFloat(int y, const float* src);

    /// Copy converted to another storage. Returns a null surface on allocation
    /// failure. Converting to the storage it already has still copies.
    [[nodiscard]] PixelSurface convertedTo(PixelStorage storage) const;

private:
    std::vector<uint8_t> m_data;
    int m_width = 0;
    int m_height = 0;
    qsizetype m_bytesPerLine = 0;
    PixelStorage m_storage = PixelStorage::UInt8;
    PixelAlpha m_alpha = PixelAlpha::Straight;
};

} // namespace ruwa::shared::imaging

#endif // RUWA_SHARED_IMAGING_PIXELSURFACE_H
