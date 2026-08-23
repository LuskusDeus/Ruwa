// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E T T I N G S
// ==========================================================================

#include "features/export/ExportSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>

namespace ruwa::core::exporting {

namespace {

constexpr auto kSettingsGroup = "CanvasExport";

QString settingsKey(const char* name)
{
    return QStringLiteral("%1/%2").arg(QLatin1String(kSettingsGroup), QLatin1String(name));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
//   F O R M A T   C A P A B I L I T I E S
// ---------------------------------------------------------------------------

ExportFormatCapabilities formatCapabilities(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Png:
        return { true, true, false, "png", "png" };
    case ExportFormat::Jpeg:
        return { false, false, true, "jpg", "jpeg" };
    case ExportFormat::WebP:
        // Qt writes WebP through libwebp at 8 bits per channel; quality 100
        // selects its lossless mode. There is no 16-bit WebP to offer.
        return { true, false, true, "webp", "webp" };
    }
    return { true, true, false, "png", "png" };
}

QStringList knownImageSuffixes()
{
    return { QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("webp") };
}

QString formatDisplayName(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Png:
        return QStringLiteral("PNG");
    case ExportFormat::Jpeg:
        return QStringLiteral("JPEG");
    case ExportFormat::WebP:
        return QStringLiteral("WebP");
    }
    return QStringLiteral("PNG");
}

QString formatToString(ExportFormat format)
{
    return formatDisplayName(format);
}

ExportFormat formatFromString(const QString& text, ExportFormat fallback)
{
    const QString normalized = text.trimmed().toUpper();
    if (normalized == QLatin1String("PNG")) {
        return ExportFormat::Png;
    }
    if (normalized == QLatin1String("JPEG") || normalized == QLatin1String("JPG")) {
        return ExportFormat::Jpeg;
    }
    if (normalized == QLatin1String("WEBP")) {
        return ExportFormat::WebP;
    }
    return fallback;
}

// ---------------------------------------------------------------------------
//   P A T H
// ---------------------------------------------------------------------------

QString ExportSettings::fileName() const
{
    const QString suffix = QString::fromLatin1(formatCapabilities(format).suffix);

    QString name = baseName.trimmed();
    if (name.isEmpty()) {
        name = QCoreApplication::translate("ExportSettings", "Untitled");
    }

    const QString currentSuffix = QFileInfo(name).suffix();
    if (!currentSuffix.isEmpty()) {
        if (currentSuffix.compare(suffix, Qt::CaseInsensitive) == 0) {
            return name;
        }
        // JPEG is the one format with two spellings; ".jpeg" is already right.
        if (format == ExportFormat::Jpeg
            && currentSuffix.compare(QLatin1String("jpeg"), Qt::CaseInsensitive) == 0) {
            return name;
        }
        if (knownImageSuffixes().contains(currentSuffix, Qt::CaseInsensitive)) {
            name.chop(currentSuffix.length() + 1); // + the dot
        }
    }

    return name + QLatin1Char('.') + suffix;
}

QString ExportSettings::absolutePath() const
{
    if (directory.trimmed().isEmpty()) {
        return {};
    }
    return QDir(directory).absoluteFilePath(fileName());
}

// ---------------------------------------------------------------------------
//   P E R S I S T E N C E
// ---------------------------------------------------------------------------

void ExportSettings::loadPreferences()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    format = formatFromString(settings.value(settingsKey("format")).toString(), format);
    quality = settings.value(settingsKey("quality"), quality).toInt();
    bitDepth = settings.value(settingsKey("bitDepth"), 8).toInt() >= 16 ? ExportBitDepth::Bit16
                                                                       : ExportBitDepth::Bit8;
    transparentBackground
        = settings.value(settingsKey("transparentBackground"), transparentBackground).toBool();
    includeCanvasBackground
        = settings.value(settingsKey("includeCanvasBackground"), includeCanvasBackground).toBool();

    const QColor storedMatte(settings.value(settingsKey("matteColor")).toString());
    if (storedMatte.isValid()) {
        matteColor = storedMatte;
    }

    const int filterValue = settings.value(settingsKey("resampleFilter"),
                                        static_cast<int>(resampleFilter))
                                .toInt();
    if (filterValue >= static_cast<int>(shared::imaging::ResampleFilter::Nearest)
        && filterValue <= static_cast<int>(shared::imaging::ResampleFilter::Lanczos3)) {
        resampleFilter = static_cast<shared::imaging::ResampleFilter>(filterValue);
    }

    const QString storedDirectory = settings.value(settingsKey("directory")).toString();
    if (!storedDirectory.isEmpty() && QDir(storedDirectory).exists()) {
        directory = storedDirectory;
    }
}

void ExportSettings::savePreferences() const
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    settings.setValue(settingsKey("format"), formatToString(format));
    settings.setValue(settingsKey("quality"), quality);
    settings.setValue(settingsKey("bitDepth"), bitDepth == ExportBitDepth::Bit16 ? 16 : 8);
    settings.setValue(settingsKey("transparentBackground"), transparentBackground);
    settings.setValue(settingsKey("includeCanvasBackground"), includeCanvasBackground);
    settings.setValue(settingsKey("matteColor"), matteColor.name(QColor::HexRgb));
    settings.setValue(settingsKey("resampleFilter"), static_cast<int>(resampleFilter));
    settings.setValue(settingsKey("directory"), directory);
}

// ---------------------------------------------------------------------------
//   V A L I D A T I O N
// ---------------------------------------------------------------------------

ExportValidation validate(ExportSettings& settings)
{
    ExportValidation result;

    const ExportFormatCapabilities caps = formatCapabilities(settings.format);

    const int clampedQuality = std::clamp(settings.quality, 1, 100);
    if (clampedQuality != settings.quality) {
        settings.quality = clampedQuality;
    }

    if (settings.bitDepth == ExportBitDepth::Bit16 && !caps.supports16Bit) {
        settings.bitDepth = ExportBitDepth::Bit8;
        result.warnings << QCoreApplication::translate("ExportSettings",
            "%1 cannot store 16 bits per channel — exported at 8 bits.")
                               .arg(formatDisplayName(settings.format));
    }

    if (settings.transparentBackground && !caps.supportsAlpha) {
        settings.transparentBackground = false;
        result.warnings << QCoreApplication::translate("ExportSettings",
            "%1 has no alpha channel — transparent areas were filled with the matte color.")
                               .arg(formatDisplayName(settings.format));
    }

    if (!settings.matteColor.isValid()) {
        settings.matteColor = QColor(255, 255, 255);
    }

    if (settings.directory.trimmed().isEmpty()) {
        result.error
            = QCoreApplication::translate("ExportSettings", "No destination folder is set.");
        return result;
    }

    const QSize size = settings.outputSize;
    if (size.width() <= 0 || size.height() <= 0) {
        result.error = QCoreApplication::translate("ExportSettings", "The export size is empty.");
        return result;
    }
    if (size.width() > kMaxOutputDimension || size.height() > kMaxOutputDimension) {
        result.error = QCoreApplication::translate("ExportSettings",
            "The export size is %1 x %2 px. The largest side supported is %3 px.")
                           .arg(size.width())
                           .arg(size.height())
                           .arg(kMaxOutputDimension);
        return result;
    }

    const qint64 pixels = static_cast<qint64>(size.width()) * static_cast<qint64>(size.height());
    if (pixels > kMaxOutputPixels) {
        result.error = QCoreApplication::translate("ExportSettings",
            "The export size is %1 x %2 px (%3 megapixels). The limit is %4 megapixels.")
                           .arg(size.width())
                           .arg(size.height())
                           .arg(pixels / (1024 * 1024))
                           .arg(kMaxOutputPixels / (1024 * 1024));
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace ruwa::core::exporting
