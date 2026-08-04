// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   C O N T E N T   S O U R C E
// ============================================================================

#ifndef RUWA_CORE_LAYERS_SMARTCONTENTSOURCE_H
#define RUWA_CORE_LAYERS_SMARTCONTENTSOURCE_H

#include <QByteArray>
#include <QString>

namespace ruwa::core::layers {

/**
 * @brief Identify the file a smart object's content was built from.
 *
 * A smart content records where it can be REFRESHED from, never where it lives:
 * the pixels are always written into the document (see the smart-object plan),
 * so an unreachable source is not an error. The hash exists so a later "this
 * file changed on disk, reload it?" can tell a moved file from a modified one,
 * for objects created today as well as after linking ships.
 *
 * SHA-256 over the raw file bytes, streamed in chunks so a large PSD/PNG is not
 * loaded twice. Returns an empty array when the file cannot be read — callers
 * treat that as "no fingerprint", not as a failure of the operation itself.
 *
 * A cheap size+mtime pre-check belongs in front of this call once refreshing
 * exists; hashing is the confirmation step, not the polling step.
 */
QByteArray smartSourceFileHash(const QString& filePath);

/// Absolute, cleaned path recorded as a content's sourcePath. Empty in, empty out.
QString normalizedSmartSourcePath(const QString& filePath);

} // namespace ruwa::core::layers

#endif // RUWA_CORE_LAYERS_SMARTCONTENTSOURCE_H
