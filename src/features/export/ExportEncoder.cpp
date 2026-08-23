// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   E N C O D E R
// ==========================================================================

#include "features/export/ExportEncoder.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ruwa::core::exporting::encoder {

namespace {

using shared::imaging::kPixelChannels;
using shared::imaging::PixelAlpha;
using shared::imaging::PixelStorage;
using shared::imaging::PixelSurface;

inline uint8_t toByte(float value)
{
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

inline uint16_t toWord(float value)
{
    return static_cast<uint16_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f));
}

struct MatteRgb {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

MatteRgb matteOf(const QColor& color)
{
    const QColor rgb = color.isValid() ? color.toRgb() : QColor(255, 255, 255);
    return { static_cast<float>(rgb.redF()), static_cast<float>(rgb.greenF()),
        static_cast<float>(rgb.blueF()) };
}

} // anonymous namespace

QImage toQImage(PixelSurface& surface, const ExportSettings& settings)
{
    if (surface.isNull()) {
        return {};
    }

    // The capture arrives premultiplied and stays that way through resampling.
    // Image files store straight alpha, so this is the single point where the
    // division happens — once, over the final (possibly much smaller) image.
    surface.convertAlphaMode(PixelAlpha::Straight);

    const ExportFormatCapabilities caps = formatCapabilities(settings.format);
    const bool keepAlpha = settings.transparentBackground && caps.supportsAlpha;
    const bool deep = settings.bitDepth == ExportBitDepth::Bit16 && caps.supports16Bit;

    const int w = surface.width();
    const int h = surface.height();

    // Zero-copy path: the surface already holds exactly the bytes an
    // RGBA8888 QImage wants, in the same order and with the same stride.
    if (!deep && keepAlpha && surface.storage() == PixelStorage::UInt8) {
        return QImage(surface.bits(), w, h, static_cast<qsizetype>(surface.bytesPerLine()),
            QImage::Format_RGBA8888);
    }

    QImage::Format targetFormat = QImage::Format_RGBA8888;
    if (deep) {
        targetFormat = keepAlpha ? QImage::Format_RGBA64 : QImage::Format_RGBX64;
    } else if (!keepAlpha) {
        targetFormat = QImage::Format_RGB888;
    }

    QImage image(w, h, targetFormat);
    if (image.isNull()) {
        return {};
    }

    const MatteRgb matte = matteOf(settings.matteColor);
    std::vector<float> row(static_cast<size_t>(w) * kPixelChannels);

    for (int y = 0; y < h; ++y) {
        surface.readRowFloat(y, row.data());

        if (!keepAlpha) {
            // Straight alpha over an opaque matte. Without this, every pixel
            // the user painted nothing on arrives at a format with no alpha
            // channel carrying whatever color the clear left behind — black.
            for (int x = 0; x < w; ++x) {
                float* p = row.data() + static_cast<size_t>(x) * kPixelChannels;
                const float a = std::clamp(p[3], 0.0f, 1.0f);
                const float inv = 1.0f - a;
                p[0] = p[0] * a + matte.r * inv;
                p[1] = p[1] * a + matte.g * inv;
                p[2] = p[2] * a + matte.b * inv;
                p[3] = 1.0f;
            }
        }

        if (deep) {
            auto* dst = reinterpret_cast<uint16_t*>(image.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const float* p = row.data() + static_cast<size_t>(x) * kPixelChannels;
                dst[x * 4 + 0] = toWord(p[0]);
                dst[x * 4 + 1] = toWord(p[1]);
                dst[x * 4 + 2] = toWord(p[2]);
                // RGBX64 ignores the fourth component but still stores it;
                // writing opaque keeps the buffer meaningful if it is ever
                // reinterpreted as RGBA64.
                dst[x * 4 + 3] = toWord(keepAlpha ? p[3] : 1.0f);
            }
        } else if (keepAlpha) {
            uint8_t* dst = image.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const float* p = row.data() + static_cast<size_t>(x) * kPixelChannels;
                dst[x * 4 + 0] = toByte(p[0]);
                dst[x * 4 + 1] = toByte(p[1]);
                dst[x * 4 + 2] = toByte(p[2]);
                dst[x * 4 + 3] = toByte(p[3]);
            }
        } else {
            uint8_t* dst = image.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const float* p = row.data() + static_cast<size_t>(x) * kPixelChannels;
                dst[x * 3 + 0] = toByte(p[0]);
                dst[x * 3 + 1] = toByte(p[1]);
                dst[x * 3 + 2] = toByte(p[2]);
            }
        }
    }

    return image;
}

WriteOutcome writeImage(
    const QImage& image, const QString& absolutePath, const ExportSettings& settings)
{
    WriteOutcome outcome;

    if (image.isNull()) {
        outcome.errorText
            = QCoreApplication::translate("ExportEncoder", "There is nothing to write.");
        return outcome;
    }
    if (absolutePath.isEmpty()) {
        outcome.errorText
            = QCoreApplication::translate("ExportEncoder", "The destination path is empty.");
        return outcome;
    }

    const ExportFormatCapabilities caps = formatCapabilities(settings.format);

    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly)) {
        outcome.errorText = file.errorString();
        return outcome;
    }

    QImageWriter writer(&file, QByteArray(caps.imageWriterFormat));
    if (caps.supportsQuality) {
        writer.setQuality(settings.quality);
    }

    if (!writer.write(image)) {
        const QString reason = writer.errorString();
        file.cancelWriting();
        outcome.errorText = reason;
        return outcome;
    }

    if (!file.commit()) {
        outcome.errorText = file.errorString();
        return outcome;
    }

    outcome.ok = true;
    outcome.fileSizeBytes = QFileInfo(absolutePath).size();
    return outcome;
}

namespace {

// The sample is estimated through the same format decisions toQImage() makes
// for the real export, so the encoder sees the same kind of input.
QImage prepareEstimateImage(const QImage& sample, const ExportSettings& settings)
{
    const ExportFormatCapabilities caps = formatCapabilities(settings.format);
    const bool keepAlpha = settings.transparentBackground && caps.supportsAlpha;
    const bool deep = settings.bitDepth == ExportBitDepth::Bit16 && caps.supports16Bit;

    if (deep) {
        return sample.convertToFormat(
            keepAlpha ? QImage::Format_RGBA64 : QImage::Format_RGBX64, Qt::ColorOnly);
    }
    if (keepAlpha) {
        return sample.convertToFormat(QImage::Format_RGBA8888, Qt::ColorOnly);
    }

    // Straight alpha over an opaque matte — same compositing the real path
    // performs per-row in toQImage().
    QImage matted(sample.size(), QImage::Format_RGB888);
    if (matted.isNull()) {
        return {};
    }
    const QColor matte
        = settings.matteColor.isValid() ? settings.matteColor : QColor(255, 255, 255);
    QPainter painter(&matted);
    painter.fillRect(matted.rect(), matte);
    painter.drawImage(0, 0, sample);
    painter.end();
    return matted;
}

} // anonymous namespace

SizeEstimate estimateFileSize(
    const QImage& sample, const QSize& outputSize, const ExportSettings& settings)
{
    SizeEstimate result;

    const qint64 outputPixels
        = static_cast<qint64>(qMax(0, outputSize.width())) * qMax(0, outputSize.height());
    if (sample.isNull() || outputPixels <= 0) {
        return result;
    }

    const QImage prepared = prepareEstimateImage(sample, settings);
    if (prepared.isNull() || prepared.size().isEmpty()) {
        return result;
    }

    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);

    const ExportFormatCapabilities caps = formatCapabilities(settings.format);
    QImageWriter writer(&buffer, QByteArray(caps.imageWriterFormat));
    if (caps.supportsQuality) {
        writer.setQuality(settings.quality);
    }
    if (!writer.write(prepared)) {
        return result;
    }

    // The sample carries the same picture at fewer pixels, so its measured
    // bytes-per-pixel scales linearly. PNG is the least faithful format here —
    // deflate exploits repeats a full-size image has and a minified one has
    // not — but it stays within the "~" honesty of the label.
    const qint64 samplePixels
        = static_cast<qint64>(prepared.width()) * prepared.height();
    result.bytes = qMax<qint64>(1,
        static_cast<qint64>(std::llround(
            static_cast<double>(encoded.size()) * static_cast<double>(outputPixels)
            / static_cast<double>(samplePixels))));
    result.ok = true;
    return result;
}

} // namespace ruwa::core::exporting::encoder
