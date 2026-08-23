// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   E N C O D E R
// ==========================================================================
//
//   PixelSurface -> QImage -> file. Two steps, both worker-thread safe.
//
//   The QImage step is where bit depth and the matte are decided:
//     8-bit + alpha  -> Format_RGBA8888, wrapping the surface's own memory
//     8-bit, matted  -> Format_RGB888
//     16-bit + alpha -> Format_RGBA64
//     16-bit, matted -> Format_RGBX64
//   The first case copies nothing: QImage can borrow an external buffer, and a
//   full-size copy is exactly what an export of a large document cannot afford
//   to make for free. The returned QImage then BORROWS the surface — see the
//   lifetime note on toQImage().
//
//   The file step goes through QSaveFile: an export that fails halfway (disk
//   full, permission lost, process killed) must not leave a truncated file
//   where a good one used to be.
//

#ifndef RUWA_CORE_EXPORTING_EXPORTENCODER_H
#define RUWA_CORE_EXPORTING_EXPORTENCODER_H

#include "features/export/ExportSettings.h"
#include "shared/imaging/PixelSurface.h"

#include <QImage>
#include <QString>

namespace ruwa::core::exporting::encoder {

/// Build the QImage to hand the writer.
///
/// LIFETIME: for an 8-bit alpha-keeping export the result shares `surface`'s
/// buffer instead of copying it, so `surface` must outlive the returned image
/// and must not be modified while it exists. Every other combination returns
/// an independent image; callers should treat the borrowing case as the rule
/// and simply keep the surface alive.
///
/// The surface is taken by non-const reference because this is where the
/// pipeline's one and only unpremultiply happens — in place, on the caller's
/// buffer, rather than into a second full-size copy.
[[nodiscard]] QImage toQImage(
    shared::imaging::PixelSurface& surface, const ExportSettings& settings);

struct WriteOutcome {
    bool ok = false;
    QString errorText;
    qint64 fileSizeBytes = 0;
};

/// Write `image` to `absolutePath` atomically. Never partially overwrites an
/// existing file: the bytes land in a sibling temp file that replaces the
/// target only after a complete, successful encode.
[[nodiscard]] WriteOutcome writeImage(
    const QImage& image, const QString& absolutePath, const ExportSettings& settings);

} // namespace ruwa::core::exporting::encoder

#endif // RUWA_CORE_EXPORTING_EXPORTENCODER_H
