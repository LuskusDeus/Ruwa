// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   C O N T E N T   S O U R C E
// ============================================================================

#include "features/layers/model/SmartContentSource.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace ruwa::core::layers {
namespace {

// Big enough to keep the read syscall count low on multi-hundred-megabyte
// sources, small enough that hashing never doubles the memory an import needs.
constexpr qint64 kHashChunkBytes = 1 << 20;

} // namespace

QByteArray smartSourceFileHash(const QString& filePath)
{
    if (filePath.trimmed().isEmpty()) {
        return {};
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray chunk;
    while (!file.atEnd()) {
        chunk = file.read(kHashChunkBytes);
        if (chunk.isEmpty()) {
            // Read error partway through: an incomplete digest would compare
            // unequal to itself on the next run, so report no fingerprint.
            return {};
        }
        hash.addData(chunk);
    }

    return hash.result();
}

QString normalizedSmartSourcePath(const QString& filePath)
{
    const QString trimmed = filePath.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return QFileInfo(trimmed).absoluteFilePath();
}

} // namespace ruwa::core::layers
