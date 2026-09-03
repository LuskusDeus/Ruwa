// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_ABRBRUSHIMPORTER_H
#define RUWA_CORE_BRUSHES_ABRBRUSHIMPORTER_H

#include <QStringList>
#include <QVariantMap>
#include <QString>
#include <QVector>

namespace ruwa::core::brushes {

struct AbrImportedTip {
    QString name;
    /// Absolute path of the extracted tip bitmap, empty for a computed round
    /// brush that carries no sampled image.
    QString imagePath;
    QVariantMap settings;
    /// Photoshop features this brush uses that Ruwa has no equivalent for, one
    /// human-readable line each. Callers may surface these; ignoring them only
    /// loses the explanation, never the import.
    QStringList unsupported;
};

bool importAbrBrushTips(
    const QString& filePath, QVector<AbrImportedTip>& tips, QString* errorMessage = nullptr);

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_ABRBRUSHIMPORTER_H
