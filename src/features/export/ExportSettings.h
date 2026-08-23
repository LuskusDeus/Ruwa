// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E T T I N G S
// ==========================================================================
//
//   Everything an export needs, in one value that carries no UI and no GL.
//
//   The panel edits it, the service consumes it, QSettings persists it. The
//   source rectangle is deliberately NOT in here: which part of the document
//   gets exported is the canvas export frame, owned by CanvasPanel, and
//   duplicating it into a settings object would create two answers to the same
//   question.
//
//   Capabilities differ per format (JPEG has no alpha, only PNG carries 16 bits
//   per channel). Rather than let the UI guess, formatCapabilities() states
//   them once and validate() clamps a settings object to them while REPORTING
//   what it changed — an export that silently drops the depth the user asked
//   for is worse than one that says it did.
//

#ifndef RUWA_CORE_EXPORTING_EXPORTSETTINGS_H
#define RUWA_CORE_EXPORTING_EXPORTSETTINGS_H

#include "shared/imaging/ImageResampler.h"

#include <QColor>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace ruwa::core::exporting {

enum class ExportFormat : uint8_t {
    Png = 0,
    Jpeg = 1,
    WebP = 2,
};

enum class ExportBitDepth : uint8_t {
    Bit8 = 0, ///< 8 bits per channel
    Bit16 = 1, ///< 16 bits per channel (PNG only)
};

struct ExportFormatCapabilities {
    bool supportsAlpha = true;
    bool supports16Bit = false;
    bool supportsQuality = false;
    const char* suffix = "png";
    const char* imageWriterFormat = "png";
};

[[nodiscard]] ExportFormatCapabilities formatCapabilities(ExportFormat format);

/// Suffixes this application knows how to write, used to decide whether a
/// user-typed file name already carries an extension that should be REPLACED
/// rather than appended to ("art.jpg" exported as PNG must not become
/// "art.jpg.png").
[[nodiscard]] QStringList knownImageSuffixes();

[[nodiscard]] QString formatDisplayName(ExportFormat format);
[[nodiscard]] ExportFormat formatFromString(const QString& text, ExportFormat fallback);
[[nodiscard]] QString formatToString(ExportFormat format);

struct ExportSettings {
    ExportFormat format = ExportFormat::Png;

    /// 1-100. Meaningful for JPEG and WebP; ignored for PNG, whose file size is
    /// a lossless compression trade-off rather than an image-quality one.
    int quality = 92;

    ExportBitDepth bitDepth = ExportBitDepth::Bit8;

    /// Keep the alpha channel. When false — or when the format has no alpha —
    /// the image is composited over `matteColor` first. Writing a transparent
    /// canvas to JPEG without a matte produces black fringes, so the matte is
    /// not optional, only its color is.
    bool transparentBackground = true;
    QColor matteColor = QColor(255, 255, 255);

    /// Whether the document background layer participates. Independent of
    /// `transparentBackground`: a visible background makes the image opaque on
    /// its own, this switch decides whether it is drawn at all.
    bool includeCanvasBackground = true;

    /// Final pixel size. Authoritative — the UI's scale percentage is a way to
    /// compute this, not a second source of truth, and is therefore recomputed
    /// from the export frame when the panel opens rather than persisted.
    QSize outputSize;

    shared::imaging::ResampleFilter resampleFilter = shared::imaging::ResampleFilter::Bicubic;

    QString directory;
    QString baseName;

    /// `baseName` with the format's suffix applied: kept if already correct,
    /// replaced if it is some other known image suffix, appended otherwise.
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] QString absolutePath() const;

    /// Load / store the parts that are a user preference rather than a
    /// per-export decision. `outputSize` is not persisted: it belongs to the
    /// document being exported, not to the application.
    void loadPreferences();
    void savePreferences() const;
};

struct ExportValidation {
    bool ok = false;
    QString error; ///< Set when the export cannot proceed at all.
    QStringList warnings; ///< Set when settings were clamped but export can run.
};

/// Clamp `settings` to what the chosen format can actually do and to the
/// pixel/memory limits, reporting every adjustment. Call before starting a job;
/// ExportService calls it again itself, so a caller that skips it still cannot
/// smuggle an impossible request through.
[[nodiscard]] ExportValidation validate(ExportSettings& settings);

/// Largest output the exporter accepts, per side and in total pixels. A frame
/// dragged across an infinite canvas can otherwise ask for an allocation no
/// machine will satisfy, and the failure would land as a bad_alloc mid-write.
constexpr int kMaxOutputDimension = 65535;
constexpr qint64 kMaxOutputPixels = 512LL * 1024LL * 1024LL; // 512 Mpx

} // namespace ruwa::core::exporting

#endif // RUWA_CORE_EXPORTING_EXPORTSETTINGS_H
